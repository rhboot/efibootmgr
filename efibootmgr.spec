Summary: EFI Boot Manager
Name: efibootmgr
Version: 0.5.0
Release: 0
Group: System Environment/Base
Copyright: GPL
Vendor: Dell linux.dell.com
Packager: Matt Domsch <Matt_Domsch@dell.com>


Source0: http://linux.dell.com/efibootmgr/permalink/efibootmgr-%{version}.tar.gz
Source1: http://linux.dell.com/efibootmgr/permalink/efibootmgr-%{version}.tar.gz.sign

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
* Thu Aug 24 2004 Matt Domsch <Matt_Domsch@dell.com>
- new home linux.dell.com

* Fri May 18 2001 Matt Domsch <Matt_Domsch@dell.com>
- See doc/ChangeLog
