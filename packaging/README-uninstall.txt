KoboTerminal Uninstall Package
==============================

Contents:
  - uninstall.sh                         (Uninstall script)
  - .adds/koboterminal/libkoboterminal.so  (Reference copy)

What will be removed:
  1. /mnt/onboard/.adds/koboterminal/libkoboterminal.so
     - The ARM shared library loaded by Nickel from .adds/ directory

  2. /mnt/onboard/.adds/koboterminal/ directory
     - The directory containing the shared library (if empty)

Installation Steps:
  1. Copy the package to your Kobo device:
     scp koboterminal-uninstall.tgz root@<KOBO_IP>:/tmp/

  2. Extract on the Kobo:
     ssh root@<KOBO_IP> 'cd /tmp && tar xzf koboterminal-uninstall.tgz'

  3. Run the uninstall script:
     /tmp/uninstall.sh

  4. Restart Nickel (Settings → Quit) or reboot

After uninstallation:
  - Nickel will no longer load the koboterminal library
  - The terminal emulator will not be available
  - All configuration files are removed
