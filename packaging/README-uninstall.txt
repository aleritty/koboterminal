KoboTerminal Uninstall
======================

What will be removed:
  1. /mnt/onboard/.adds/koboterminal/libkoboterminal.so
     - The ARM shared library loaded by Nickel from .adds/ directory

  2. /mnt/onboard/.adds/koboterminal/ directory
     - The directory containing the shared library (if empty)

Uninstall Steps:
  1. Copy the uninstall script to your Kobo device:
     scp packaging/uninstall.sh root@<KOBO_IP>:/tmp/

  2. Run the uninstall script:
     ssh root@<KOBO_IP> '/tmp/uninstall.sh'

  3. Restart Nickel (Settings → Quit) or reboot

After uninstallation:
  - Nickel will no longer load the koboterminal library
  - The terminal emulator will not be available
  - All configuration files are removed
