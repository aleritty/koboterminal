/*
 * KoboTerminal server: a standalone daemon (no Qt, no libnickel, no dlopen
 * tricks) that serves a tiny local website — xterm.js talking over a raw
 * WebSocket to a forkpty()'d shell. NickelMenu's "Terminal" entry spawns
 * this in the background, then chains into nickel_browser to open it. All
 * escape-sequence interpretation happens client-side in xterm.js; this
 * daemon just relays bytes.
 *
 * Single-threaded and sequential: the process's own lifetime IS the one
 * terminal session it serves. Binding the listen socket doubles as the
 * "only one instance" check — a second invocation's bind() fails and it
 * exits quietly, which is what makes the cmd_spawn menu entry idempotent.
 *
 * Every path below logs to stderr (redirected to kterm-server.log by the
 * cmd_spawn line) — startup, every failure, and why the session ended —
 * since this runs headless with no other way to see what happened.
 */

#define _GNU_SOURCE

#include "sha1.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pty.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define KTERM_PORT 8090
#ifndef KTERM_ASSET_DIR
#define KTERM_ASSET_DIR "/mnt/onboard/.adds/koboterminal"
#endif
#define KTERM_COLS 80
#define KTERM_ROWS 24
#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define REQ_BUF_CAP 8192

/* Every accepted connection gets read/write timeouts. Without this, a
 * client that stops draining its socket (e.g. stuck showing a "downloading"
 * dialog instead of rendering the page) makes our write() calls block
 * forever — and since this daemon is single-threaded, that one stuck
 * connection wedges the whole process, including future connections. */
#define KTERM_SOCKET_TIMEOUT_SEC 15

static void set_socket_timeout(int fd) {
  struct timeval tv;
  tv.tv_sec = KTERM_SOCKET_TIMEOUT_SEC;
  tv.tv_usec = 0;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

static void logmsg(const char *fmt, ...) {
  char timebuf[32];
  time_t now = time(NULL);
  struct tm *tm_info = localtime(&now);
  va_list args;

  if (tm_info) {
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm_info);
  } else {
    snprintf(timebuf, sizeof(timebuf), "time-unavailable");
  }

  fprintf(stderr, "[%s] ", timebuf);
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fputc('\n', stderr);
  fflush(stderr);
}

static ssize_t read_full(int fd, void *buf, size_t n) {
  size_t got = 0;
  while (got < n) {
    ssize_t r = read(fd, (char *)buf + got, n - got);
    if (r <= 0) {
      return got > 0 ? (ssize_t)got : r;
    }
    got += (size_t)r;
  }
  return (ssize_t)got;
}

static ssize_t write_full(int fd, const void *buf, size_t n) {
  size_t sent = 0;
  while (sent < n) {
    ssize_t w = write(fd, (const char *)buf + sent, n - sent);
    if (w <= 0) {
      return sent > 0 ? (ssize_t)sent : w;
    }
    sent += (size_t)w;
  }
  return (ssize_t)sent;
}

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void base64_encode(const unsigned char *data, size_t len, char *out,
                           size_t out_cap) {
  size_t i = 0, j = 0;

  while (i + 2 < len && j + 4 < out_cap) {
    uint32_t n = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8) |
                 data[i + 2];
    out[j++] = b64_table[(n >> 18) & 63];
    out[j++] = b64_table[(n >> 12) & 63];
    out[j++] = b64_table[(n >> 6) & 63];
    out[j++] = b64_table[n & 63];
    i += 3;
  }

  size_t rem = len - i;
  if (rem == 1 && j + 4 < out_cap) {
    uint32_t n = (uint32_t)data[i] << 16;
    out[j++] = b64_table[(n >> 18) & 63];
    out[j++] = b64_table[(n >> 12) & 63];
    out[j++] = '=';
    out[j++] = '=';
  } else if (rem == 2 && j + 4 < out_cap) {
    uint32_t n = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8);
    out[j++] = b64_table[(n >> 18) & 63];
    out[j++] = b64_table[(n >> 12) & 63];
    out[j++] = b64_table[(n >> 6) & 63];
    out[j++] = '=';
  }
  out[j] = '\0';
}

/* ============================================================================
 * WebSocket framing (RFC 6455) — text/binary frames only, no fragmentation.
 * ============================================================================ */

static ssize_t ws_recv_frame(int fd, unsigned char *buf, size_t cap,
                              int *opcode) {
  unsigned char hdr[2];
  if (read_full(fd, hdr, 2) != 2) {
    return -1;
  }

  *opcode = hdr[0] & 0x0F;
  int masked = (hdr[1] & 0x80) != 0;
  uint64_t len = hdr[1] & 0x7F;

  if (len == 126) {
    unsigned char ext[2];
    if (read_full(fd, ext, 2) != 2) {
      return -1;
    }
    len = ((uint64_t)ext[0] << 8) | ext[1];
  } else if (len == 127) {
    unsigned char ext[8];
    if (read_full(fd, ext, 8) != 8) {
      return -1;
    }
    len = 0;
    for (int i = 0; i < 8; i++) {
      len = (len << 8) | ext[i];
    }
  }

  unsigned char mask[4] = {0, 0, 0, 0};
  if (masked) {
    if (read_full(fd, mask, 4) != 4) {
      return -1;
    }
  }

  if (len > cap) {
    return -1;
  }
  if (len > 0 && read_full(fd, buf, (size_t)len) != (ssize_t)len) {
    return -1;
  }
  if (masked) {
    for (uint64_t i = 0; i < len; i++) {
      buf[i] ^= mask[i % 4];
    }
  }

  return (ssize_t)len;
}

static int ws_send_frame(int fd, int opcode, const void *data, size_t len) {
  unsigned char hdr[10];
  size_t hlen = 0;

  hdr[hlen++] = (unsigned char)(0x80 | (opcode & 0x0F));
  if (len <= 125) {
    hdr[hlen++] = (unsigned char)len;
  } else if (len <= 0xFFFF) {
    hdr[hlen++] = 126;
    hdr[hlen++] = (unsigned char)((len >> 8) & 0xFF);
    hdr[hlen++] = (unsigned char)(len & 0xFF);
  } else {
    hdr[hlen++] = 127;
    for (int i = 7; i >= 0; i--) {
      hdr[hlen++] = (unsigned char)((len >> (i * 8)) & 0xFF);
    }
  }

  if (write_full(fd, hdr, hlen) != (ssize_t)hlen) {
    return -1;
  }
  if (len > 0 && write_full(fd, data, len) != (ssize_t)len) {
    return -1;
  }
  return 0;
}

/* ============================================================================
 * Minimal HTTP: parse a request line + headers, dispatch to static file
 * serving or a WebSocket upgrade.
 * ============================================================================ */

static int read_http_request(int fd, char *buf, size_t cap, char **path_out,
                              char **ws_key_out, int *is_ws_out) {
  size_t total = 0;
  ssize_t n;

  while (total < cap - 1) {
    n = read(fd, buf + total, cap - 1 - total);
    if (n <= 0) {
      return -1;
    }
    total += (size_t)n;
    buf[total] = '\0';
    if (strstr(buf, "\r\n\r\n")) {
      break;
    }
  }

  char *line_end = strstr(buf, "\r\n");
  if (!line_end) {
    return -1;
  }
  *line_end = '\0';

  char *method = strtok(buf, " ");
  char *path = strtok(NULL, " ");
  if (!method || !path) {
    return -1;
  }
  *path_out = path;

  char *headers = line_end + 2;
  *is_ws_out = strcasestr(headers, "Upgrade: websocket") != NULL;

  char *key_hdr = strcasestr(headers, "Sec-WebSocket-Key:");
  if (key_hdr) {
    key_hdr += strlen("Sec-WebSocket-Key:");
    while (*key_hdr == ' ') {
      key_hdr++;
    }
    char *key_end = strstr(key_hdr, "\r\n");
    if (key_end) {
      *key_end = '\0';
    }
    *ws_key_out = key_hdr;
  } else {
    *ws_key_out = NULL;
  }

  return 0;
}

static void http_send_status(int fd, int code, const char *msg) {
  char hdr[128];
  int n = snprintf(hdr, sizeof(hdr),
                    "HTTP/1.1 %d %s\r\nConnection: close\r\n\r\n", code, msg);
  write_full(fd, hdr, (size_t)n);
}

static void serve_static(int fd, const char *path) {
  const char *file;
  const char *ctype;

  if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
    file = "index.html";
    ctype = "text/html; charset=utf-8";
  } else if (strcmp(path, "/xterm.js") == 0) {
    file = "xterm.js";
    ctype = "application/javascript; charset=utf-8";
  } else if (strcmp(path, "/xterm.css") == 0) {
    file = "xterm.css";
    ctype = "text/css; charset=utf-8";
  } else {
    http_send_status(fd, 404, "Not Found");
    return;
  }

  char full[512];
  snprintf(full, sizeof(full), "%s/%s", KTERM_ASSET_DIR, file);

  FILE *f = fopen(full, "rb");
  if (!f) {
    http_send_status(fd, 404, "Not Found");
    return;
  }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz < 0) {
    fclose(f);
    http_send_status(fd, 500, "Internal Server Error");
    return;
  }

  char hdr[256];
  int hlen = snprintf(hdr, sizeof(hdr),
                       "HTTP/1.1 200 OK\r\nContent-Type: %s\r\n"
                       "Content-Disposition: inline\r\n"
                       "Content-Length: %ld\r\nConnection: close\r\n\r\n",
                       ctype, sz);
  write_full(fd, hdr, (size_t)hlen);

  char chunk[8192];
  size_t n;
  while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
    write_full(fd, chunk, n);
  }
  fclose(f);
}

/* ============================================================================
 * WebSocket handshake + PTY relay
 * ============================================================================ */

/* One terminal session at a time by design (the daemon's process lifetime
 * IS the session). A connection arriving on listen_fd while we're already
 * relaying (e.g. a repeat menu tap) gets an immediate, explicit rejection
 * here instead of hanging forever waiting for an accept() that won't come
 * until this session ends. */
static void reject_extra_connection(int listen_fd) {
  int cfd = accept(listen_fd, NULL, NULL);
  if (cfd < 0) {
    return;
  }
  set_socket_timeout(cfd);
  static const char resp[] =
      "HTTP/1.1 503 Service Unavailable\r\n"
      "Content-Type: text/plain\r\n"
      "Content-Length: 26\r\n"
      "Connection: close\r\n\r\n"
      "Terminal already in use.\n";
  write_full(cfd, resp, sizeof(resp) - 1);
  close(cfd);
}

static void relay_loop(int ws_fd, int pty_fd, pid_t shell_pid, int listen_fd) {
  unsigned char buf[8192];

  for (;;) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(ws_fd, &rfds);
    FD_SET(pty_fd, &rfds);
    FD_SET(listen_fd, &rfds);
    int maxfd = ws_fd > pty_fd ? ws_fd : pty_fd;
    if (listen_fd > maxfd) {
      maxfd = listen_fd;
    }

    int r = select(maxfd + 1, &rfds, NULL, NULL, NULL);
    if (r < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }

    if (FD_ISSET(listen_fd, &rfds)) {
      reject_extra_connection(listen_fd);
    }

    if (FD_ISSET(pty_fd, &rfds)) {
      ssize_t n = read(pty_fd, buf, sizeof(buf));
      if (n <= 0) {
        break;
      }
      if (ws_send_frame(ws_fd, 0x2, buf, (size_t)n) != 0) {
        break;
      }
    }

    if (FD_ISSET(ws_fd, &rfds)) {
      int opcode = -1;
      ssize_t n = ws_recv_frame(ws_fd, buf, sizeof(buf), &opcode);
      if (n < 0 || opcode == 0x8) {
        break;
      }
      if ((opcode == 0x1 || opcode == 0x2) && n > 0) {
        if (write_full(pty_fd, buf, (size_t)n) < 0) {
          break;
        }
      }
    }

    int status;
    if (waitpid(shell_pid, &status, WNOHANG) == shell_pid) {
      break;
    }
  }
}

static void handle_websocket(int fd, const char *client_key, int listen_fd) {
  char concat[128];
  snprintf(concat, sizeof(concat), "%s%s", client_key, WS_GUID);

  unsigned char digest[20];
  SHA1_CTX ctx;
  SHA1Init(&ctx);
  SHA1Update(&ctx, (const unsigned char *)concat, strlen(concat));
  SHA1Final(digest, &ctx);

  char accept_key[64];
  base64_encode(digest, 20, accept_key, sizeof(accept_key));

  char resp[256];
  int rlen = snprintf(resp, sizeof(resp),
                       "HTTP/1.1 101 Switching Protocols\r\n"
                       "Upgrade: websocket\r\n"
                       "Connection: Upgrade\r\n"
                       "Sec-WebSocket-Accept: %s\r\n\r\n",
                       accept_key);
  if (write_full(fd, resp, (size_t)rlen) != (ssize_t)rlen) {
    logmsg("websocket handshake: failed to write response: %s", strerror(errno));
    return;
  }

  struct winsize ws;
  memset(&ws, 0, sizeof(ws));
  ws.ws_col = KTERM_COLS;
  ws.ws_row = KTERM_ROWS;

  int pty_fd;
  pid_t shell_pid = forkpty(&pty_fd, NULL, NULL, &ws);
  if (shell_pid < 0) {
    logmsg("forkpty() failed: %s", strerror(errno));
    return;
  }
  if (shell_pid == 0) {
    setenv("TERM", "xterm-256color", 1);
    execl("/bin/sh", "/bin/sh", (char *)NULL);
    _exit(127);
  }

  logmsg("session started: shell pid %d", (int)shell_pid);
  relay_loop(fd, pty_fd, shell_pid, listen_fd);
  logmsg("session ended");

  kill(shell_pid, SIGHUP);
  int status;
  waitpid(shell_pid, &status, 0);
  close(pty_fd);
}

/* Returns 1 if this connection was the WebSocket terminal session (which has
 * now ended, one way or another) — the caller shuts the whole daemon down
 * when it sees that, so "close the browser" reliably ends the process
 * instead of leaving it listening forever. Returns 0 for an ordinary static
 * file request, which the caller just loops past. */
static int handle_connection(int fd, int listen_fd) {
  char buf[REQ_BUF_CAP];
  char *path, *ws_key;
  int is_ws;

  if (read_http_request(fd, buf, sizeof(buf), &path, &ws_key, &is_ws) != 0) {
    return 0;
  }

  if (is_ws && ws_key) {
    handle_websocket(fd, ws_key, listen_fd);
    return 1;
  }

  serve_static(fd, path);
  return 0;
}

int main(int argc, char **argv) {
  int port = KTERM_PORT;
  if (argc > 1) {
    port = atoi(argv[1]);
  }

  logmsg("starting, pid=%d, port=%d, asset dir=%s", (int)getpid(), port,
         KTERM_ASSET_DIR);

  signal(SIGPIPE, SIG_IGN);

  int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    logmsg("fatal: socket() failed: %s", strerror(errno));
    return 1;
  }

  int opt = 1;
  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons((uint16_t)port);

  if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
    /* Already running (or port busy) - exit quietly. This is what makes
     * the "Terminal" menu entry's cmd_spawn idempotent/safe to tap
     * repeatedly. Still logged, since "port busy" could also mean
     * something else entirely is squatting on it. */
    logmsg("bind() failed (%s) - assuming already running, exiting quietly",
           strerror(errno));
    return 0;
  }
  if (listen(listen_fd, 8) != 0) {
    logmsg("fatal: listen() failed: %s", strerror(errno));
    return 1;
  }

  logmsg("listening on 127.0.0.1:%d", port);

  /* Single-threaded, sequential: serves the handful of short static-file
   * requests (index.html, xterm.js, xterm.css) that precede the WebSocket
   * upgrade, then blocks in the terminal session's relay loop once that
   * upgrade happens. There's deliberately no fork()/concurrency here — the
   * whole point is that the process's lifetime IS the terminal session, so
   * closing the browser (which ends the WebSocket) ends the daemon too. */
  for (;;) {
    int cfd = accept(listen_fd, NULL, NULL);
    if (cfd < 0) {
      continue;
    }
    set_socket_timeout(cfd);

    int was_session = handle_connection(cfd, listen_fd);
    close(cfd);

    if (was_session) {
      break;
    }
  }

  logmsg("exiting after session end");
  close(listen_fd);
  return 0;
}
