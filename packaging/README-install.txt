KoboTerminal Install Package
============================

Contents:
  - .adds/koboterminal/libkoboterminal.so  (ARM shared library)

What will be installed:
  1. /mnt/onboard/.adds/koboterminal/libkoboterminal.so
     - The ARM shared library for the terminal emulator
     - Loaded by Nickel via Qt plugin auto-loading from .adds/ directory

Installation Steps:
  1. Copy the package to your Kobo device:
     scp koboterminal-install.tgz root@<KOBO_IP>:/tmp/

  2. Extract on the Kobo:
     ssh root@<KOBO_IP> 'cd /tmp && tar xzf koboterminal-install.tgz'

  3. Restart Nickel (Settings → Quit) or reboot

After installation:
  - Nickel will auto-load the koboterminal library from .adds/
  - The terminal emulator will be available through NickelMenu
