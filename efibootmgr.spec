Summary: EFI Boot Manager
Name: efibootmgr
Version: 0.3.0
Release: 1
Group: System Environment/Base
Copyright: GPL
Vendor: Dell Computer Corporation www.dell.com
Packager: Matt Domsch <Matt_Domsch@dell.com>
ExclusiveArch: ia64

Source0: http://domsch.com/linux/ia64/efibootmgr-%{version}.tar.gz

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

%files
/usr/sbin/efibootmgr
%doc README
%doc INSTALL


    
%changelog
* Fri May 18 2001 Matt Domsch <Matt_Domsch@dell.com>
- Padded HARDDRIVE_DEVICE_PATH out to please EFI Boot Manager.
- Incorporated patches from Andreas Schwab
  - replace __u{8,16,32,64} with uint{8,16,32,64}_t
  - use _FILE_OFFSET_BITS
  - fix a segfault
- release v0.3.0	

* Tue May 15 2001 Matt Domsch <Matt_Domsch@dell.com>
- initial external release v0.2.0

