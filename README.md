This is **efibootmgr**, a Linux user-space application to modify the Intel
Extensible Firmware Interface (EFI) Boot Manager.  This application
can create and destroy boot entries, change the boot order, change
the next running boot option, and more.

Details on the EFI Boot Manager are available from the EFI
Specification, v1.02 or above, available from: http://www.uefi.org

Note: efibootmgr requires either the efivarfs or the
legacy efivars kernel module to be loaded prior to use.

```
usage: efibootmgr [options]
        -a | --active          Set bootnum active.
        -A | --inactive        Set bootnum inactive.
        -b | --bootnum XXXX    Modify BootXXXX (hex).
        -B | --delete-bootnum  Delete bootnum.
        -c | --create          Create new variable bootnum and add to bootorder.
        -d | --disk disk       (Defaults to /dev/sda) containing loader.
        -e | --edd [1|3|-1]    Force EDD 1.0 or 3.0 creation variables, or guess.
        -E | --device num      EDD 1.0 device number (defaults to 0x80).
        -f | --reconnect       Re-connect devices after driver is loaded.
        -F | --no-reconnect    Do not re-connect devices after driver is loaded.
        -g | --gpt             Force disk w/ invalid PMBR to be treated as GPT.
        -i | --iface name      Create a netboot entry for the named interface.
        -l | --loader name     (Defaults to \elilo.efi).
        -L | --label label     Boot manager display label (defaults to "Linux").
        -n | --bootnext XXXX   Set BootNext to XXXX (hex).
        -N | --delete-bootnext Delete BootNext.
        -o | --bootorder XXXX,YYYY,ZZZZ,...     Explicitly set BootOrder (hex).
        -O | --delete-bootorder   Delete BootOrder.
        -p | --part part          (Defaults to 1) containing loader.
        -q | --quiet              Be quiet.
        -t | --timeout seconds    Boot manager timeout.
        -T | --delete-timeout     Delete Timeout value.
        -u | --unicode | --UCS-2  Pass extra args as UCS-2 (default is ASCII).
        -v | --verbose            Print additional information.
        -V | --version            Return version and exit.
        -@ | --append-binary-args Append extra variable args from
                                  file (use - to read from stdin).
```

Typical usage:

Root can use it to display the current Boot Manager settings.
```
[root@localhost ~]# efibootmgr
BootCurrent: 0004
BootNext: 0003
BootOrder: 0004,0000,0001,0002,0003
Timeout: 30 seconds
Boot0000* Diskette Drive(device:0)
Boot0001* CD-ROM Drive(device:FF)
Boot0002* Hard Drive(Device:80)/HD(Part1,Sig00112233)
Boot0003* PXE Boot: MAC(00D0B7C15D91)
Boot0004* Linux
```
This shows:
**BootCurrent** - the boot entry used to start the currently running
system.

**BootOrder** - the boot order as would appear in the boot manager.  The
boot manager tries to boot the first active entry on this list.  If
unsuccessful, it tries the next entry, and so on.

**BootNext** - the boot entry which is scheduled to be run on next boot.
This supersedes BootOrder for one boot only, and is deleted by the
boot manager after first use.  This allows you to change the next boot
behavior without changing BootOrder.

**Timeout** - the time in seconds between when the boot manager appears
on the screen until when it automatically chooses the startup value
from BootNext or BootOrder.

Five boot entries (0000 - 0004), the active/inactive flag (* means
active), and the name displayed on the screen.

Alternative use cases could be as follows:

1) An OS installer would call `efibootmgr -c`.  This assumes that
   /dev/sda1 is your EFI System Partition, and is mounted at /boot/efi.
   This creates a new boot option, called "Linux", and puts it at the top
   of the boot order list.  Options may be passed to modify the
   default behavior.  The default OS Loader is elilo.efi.

2) A system administrator wants to change the boot order.  She would
   call `efibootmgr -o 3,4` to specify PXE boot first, then Linux
   boot.

3) A system administrator wants to change the boot order for the next
   boot only.  She would call `efibootmgr -n 4` to specify that the
   Linux entry be taken on next boot.

4) A system administrator wants to delete the Linux boot option from
   the menu.  `efibootmgr -b 4 -B` deletes entry 4 and removes it
   from BootOrder.

5) A system administrator wants to create a boot option to network
   boot (PXE).  You create the boot entry with:
   `efibootmgr -c -i eth0 -L netboot`

Please direct any bugs, features, patches, etc. to the Red Hat bootloader team at https://github.com/rhboot/efibootmgr .

Meson Notes
===========

Following the port to Meson, we no longer build *in-place*.

Create a `build` directory and configure and build the project there. e.g.

    mkdir build
    cd build
    meson setup ../
    ninja

The old build targets are now as follows:

 * `make archive` -> `ninja dist`
 * `make distclean` -> `ninja clean`
 * `make scanbuild` -> `ninja scan-build`
 * `make fanalyzer` -> `meson configure -Db_sanitize=address,undefined && ninja`
 * `make efibootmgr-static` -> `meson configure -Dprefer_static=true && ninja`
 * `EFIDIR=fedora make` -> `meson configure -Defidir=fedora && ninja`
