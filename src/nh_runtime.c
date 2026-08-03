#define _GNU_SOURCE

#include "kterm/nickelhook.h"

#include <dlfcn.h>
#include <elf.h>
#include <errno.h>
#include <link.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <syslog.h>
#include <unistd.h>

#ifndef ELFW
#define ELFW(type) _ElfW(ELF, __ELF_NATIVE_CLASS, type)
#endif

#if __x86_64__
#define R_JUMP_SLOT R_X86_64_JUMP_SLOT
#else
#if __arm__
#define R_JUMP_SLOT R_ARM_JUMP_SLOT
#else
#error Please define R_JUMP_SLOT for this architecture.
#endif
#endif

#define KTERM_BOOTSTRAP_LOG_PATH "/mnt/onboard/.adds/koboterminal/lkoboterminal.bootstrap.log"
#define KTERM_BOOTSTRAP_LOG_FALLBACK_PATH "/tmp/lkoboterminal.bootstrap.log"

static void kterm_nh_trace(const char *fmt, ...) {
    FILE *f;
    va_list args;
    time_t now;
    struct tm *tm_info;
    char timebuf[64];

    f = fopen(KTERM_BOOTSTRAP_LOG_PATH, "a");
    if (!f) {
        f = fopen(KTERM_BOOTSTRAP_LOG_FALLBACK_PATH, "a");
    }
    if (!f) {
        return;
    }

    now = time(NULL);
    tm_info = localtime(&now);
    if (tm_info) {
        strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm_info);
    } else {
        snprintf(timebuf, sizeof(timebuf), "time-unavailable");
    }

    fprintf(f, "[%s] ", timebuf);

    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);

    fputc('\n', f);
    fclose(f);
}

static void kterm_nh_log(const char *fmt, ...) {
    static __thread char buf[256];
    va_list args;
    int n;

    n = snprintf(buf, sizeof(buf), "(%s) ", NickelHook.info && NickelHook.info->name ? NickelHook.info->name : "koboterminal");
    if (n < 0) {
        n = 0;
    }

    va_start(args, fmt);
    vsnprintf(&buf[n], sizeof(buf) - (size_t)n, fmt, args);
    va_end(args);

    buf[sizeof(buf) - 1] = '\0';
    syslog(LOG_DEBUG, "%s", buf);
    kterm_nh_trace("%s", buf);
}

static void *kterm_nh_dlhook(void *handle, const char *symname, void *target) {
    struct link_map *lm;

#define NH_CHECK(cond, msg) do {                                         \
        if (!(cond)) {                                                    \
            kterm_nh_log("dlhook error: %s (check failed: %s)", msg, #cond); \
            return NULL;                                                  \
        }                                                                 \
    } while (0)

    NH_CHECK(handle && symname && target, "required arguments are null");

    NH_CHECK(!dlinfo(handle, RTLD_DI_LINKMAP, &lm), "could not get link_map for lib");

    struct {
        bool plt_is_rela;
        union {
            ElfW(Addr) _;
            ElfW(Rel) *rel;
            ElfW(Rela) *rela;
        } plt;
        ElfW(Xword) plt_sz;
        ElfW(Xword) plt_ent_sz;
        ElfW(Sym) *sym;
        const char *str;
    } dyn = {0};

    for (size_t i = 0; lm->l_ld[i].d_tag != DT_NULL; i++) {
        switch (lm->l_ld[i].d_tag) {
        case DT_PLTREL: dyn.plt_is_rela = lm->l_ld[i].d_un.d_val == DT_RELA; break;
        case DT_JMPREL: dyn.plt._ = lm->l_ld[i].d_un.d_ptr; break;
        case DT_PLTRELSZ: dyn.plt_sz = lm->l_ld[i].d_un.d_val; break;
        case DT_RELENT: if (!dyn.plt_ent_sz) dyn.plt_ent_sz = lm->l_ld[i].d_un.d_val; break;
        case DT_RELAENT: if (!dyn.plt_ent_sz) dyn.plt_ent_sz = lm->l_ld[i].d_un.d_val; break;
        case DT_SYMTAB: dyn.sym = (ElfW(Sym) *)(lm->l_ld[i].d_un.d_val); break;
        case DT_STRTAB: dyn.str = (const char *)(lm->l_ld[i].d_un.d_val); break;
        }
    }

    NH_CHECK(dyn.plt_ent_sz, "plt entry size is zero");
    NH_CHECK(dyn.plt_sz % dyn.plt_ent_sz == 0, "plt size is not a multiple of entry size");
    NH_CHECK(dyn.sym && dyn.str, "missing symtab/strtab");

    for (size_t i = 0; i < dyn.plt_sz / dyn.plt_ent_sz; i++) {
        ElfW(Rel) *rel = dyn.plt_is_rela ? (ElfW(Rel) *)(&dyn.plt.rela[i]) : &dyn.plt.rel[i];
        if (ELFW(R_TYPE)(rel->r_info) != R_JUMP_SLOT) {
            continue;
        }

        ElfW(Sym) *sym = &dyn.sym[ELFW(R_SYM)(rel->r_info)];
        const char *str = &dyn.str[sym->st_name];
        if (strcmp(str, symname)) {
            continue;
        }

        void **gotoff = (void **)(lm->l_addr + rel->r_offset);

        NH_CHECK(ELFW(ST_TYPE)(sym->st_info) == STT_FUNC, "symbol is not function");
        NH_CHECK(ELFW(ST_BIND)(sym->st_info) == STB_GLOBAL, "symbol is not global");

        void *orig = dlsym(handle, symname);
        NH_CHECK(orig, "could not dlsym symbol");

        long pagesize = sysconf(_SC_PAGESIZE);
        NH_CHECK(pagesize != -1, "could not get page size");
        void *gotpage = (void *)((size_t)(gotoff) & ~(pagesize - 1));
        NH_CHECK(!mprotect(gotpage, (size_t)pagesize, PROT_READ | PROT_WRITE), "mprotect failed");

        *gotoff = target;
        return orig;
    }

    kterm_nh_log("dlhook error: symbol not found: %s", symname);
    return NULL;

#undef NH_CHECK
}

static void *kterm_nh_hook_lib(const char *name, void *libnickel) {
    if (!strcmp(name, "libnickel.so.1.0.0") || !strcmp(name, "/usr/local/Kobo/libnickel.so.1.0.0")) {
        return libnickel;
    }
    return dlopen(name, RTLD_LAZY | RTLD_LOCAL);
}

__attribute__((constructor)) static void kterm_nh_bootstrap(void) {
    const struct nh *nh = &NickelHook;
    void *self = RTLD_DEFAULT;
    void *libnickel = NULL;
    struct nh_hook *last_hooked = NULL;
    int hooks_applied = 0;
    int hooks_failed = 0;

    kterm_nh_trace("bootstrap start");

    if (!nh->info) {
        syslog(LOG_DEBUG, "(NickelHook) fatal: NickelHook->info is NULL");
        kterm_nh_trace("fatal: NickelHook->info is NULL");
        return;
    }

    kterm_nh_trace("module name=%s", nh->info->name ? nh->info->name : "<null>");

    if (nh->info->uninstall_flag && !access(nh->info->uninstall_flag, F_OK)) {
        unlink(nh->info->uninstall_flag);
        kterm_nh_trace("uninstall flag found, exiting bootstrap");
        return;
    }

    libnickel = dlopen("libnickel.so.1.0.0", RTLD_LAZY | RTLD_NODELETE);
    if (!libnickel) {
        kterm_nh_log("fatal: dlopen libnickel failed: %s", dlerror());
        return;
    }

    for (struct nh_dlsym *v = nh->dlsym; v && v->name; v++) {
        *v->out = dlsym(RTLD_DEFAULT, v->name);
        if (!*v->out && !v->optional) {
            kterm_nh_log("fatal: could not resolve symbol '%s': %s", v->name, dlerror());
            goto rollback;
        }
    }

    for (struct nh_hook *v = nh->hook; v && v->sym; v++) {
        void *lib = kterm_nh_hook_lib(v->lib, libnickel);
        void *target;

        *v->out = NULL;

        if (!lib) {
            if (!v->optional) {
                kterm_nh_log("fatal: could not load hook lib '%s': %s", v->lib, dlerror());
                goto rollback;
            }
            ++hooks_failed;
            kterm_nh_log("warning: optional hook lib '%s' unavailable", v->lib);
            continue;
        }

        target = dlsym(self, v->sym_new);
        if (!target) {
            if (lib != libnickel) {
                dlclose(lib);
            }
            if (!v->optional) {
                kterm_nh_log("fatal: could not resolve replacement symbol '%s': %s", v->sym_new, dlerror());
                goto rollback;
            }
            ++hooks_failed;
            kterm_nh_log("warning: optional replacement symbol '%s' missing", v->sym_new);
            continue;
        }

        *v->out = kterm_nh_dlhook(lib, v->sym, target);
        if (!*v->out) {
            if (lib != libnickel) {
                dlclose(lib);
            }
            if (!v->optional) {
                kterm_nh_log("fatal: could not hook symbol '%s' in '%s'", v->sym, v->lib);
                goto rollback;
            }
            ++hooks_failed;
            kterm_nh_log("warning: optional hook failed symbol='%s' lib='%s'", v->sym, v->lib);
            continue;
        }

        ++hooks_applied;
        last_hooked = v;
        if (lib != libnickel) {
            dlclose(lib);
        }
    }

    kterm_nh_trace("hooks applied=%d failed_optional=%d", hooks_applied, hooks_failed);

    if (!last_hooked) {
        kterm_nh_trace("warning: no hooks applied");
    }

    if (nh->init && nh->init()) {
        kterm_nh_log("fatal: module init callback failed");
        goto rollback;
    }

    kterm_nh_trace("bootstrap complete");

    return;

rollback:
    kterm_nh_trace("rollback start");
    if (last_hooked) {
        for (struct nh_hook *v = last_hooked; v >= nh->hook; v--) {
            if (v->out && *v->out) {
                void *lib = kterm_nh_hook_lib(v->lib, libnickel);
                if (lib) {
                    (void)kterm_nh_dlhook(lib, v->sym, *v->out);
                    if (lib != libnickel) {
                        dlclose(lib);
                    }
                }
            }
        }
    }
    kterm_nh_trace("rollback complete");
}
