KoboTerminal Install Package
============================

Contents:
   - /usr/local/Kobo/imageformats/libkoboterminal.so  (ARM shared library)
  - .adds/koboterminal/koboterminal.conf  (hook logging configuration)

What will be installed:
  1. /usr/local/Kobo/imageformats/libkoboterminal.so
     - The ARM shared library for the terminal emulator
     - Loaded by Nickel via Qt plugin auto-loading from imageformats
  2. /mnt/onboard/.adds/koboterminal/koboterminal.conf
     - Logging configuration (`verbose=0` default)
     - Log output path: /mnt/onboard/.adds/koboterminal/lkoboterminal.log

Installation Steps:
  1. Build artifacts locally:
     scripts/build.sh

  2. Sync artifacts to Kobo:
     scripts/sync.sh

  3. Restart Nickel (Settings → Quit) or reboot

After installation:
  - Nickel will auto-load the koboterminal library from imageformats
  - The terminal emulator will be available through NickelMenu
