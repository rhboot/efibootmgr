Summary: EFI Boot Manager
Name: efibootmgr
Version: 0.4.1
Release: 0
Group: System Environment/Base
Copyright: GPL
Vendor: Dell Computer Corporation www.dell.com
Packager: Matt Domsch <Matt_Domsch@dell.com>


Source0: http://domsch.com/linux/ia64/efibootmgr/efibootmgr-%{version}.tar.gz

%description
efibootmgr displays and allows the user to edit the Intel Extensible
Firmware Interface (EFI) Boot Manager variables.  Additional
information about EFI can be found at
http://developer.intel.com/technology/efi/efi.htm. 

%prep
%setup
%build
make
%install
install --group=root --owner=root --mode 555 src/efibootmgr/efibootmgr $RPM_BUILD_ROOT/usr/sbin
install --group=root --owner=root --mode 444 src/man/man8/efibootmgr.8 $RPM_BUILD_ROOT/usr/share/man/man8

%files
/usr/sbin/efibootmgr
/usr/share/man/man8/efibootmgr.8
%doc README
%doc INSTALL

    
%changelog
* Fri May 18 2001 Matt Domsch <Matt_Domsch@dell.com>
- See doc/ChangeLog
