Summary: EFI Boot Manager
Name: efibootmgr
Version: 0.5.4
Release: 1%{?dist}
Group: System Environment/Base
License: GPLv2+
URL: http://linux.dell.com/%{name}/
BuildRequires: pciutils-devel, zlib-devel
BuildRoot: %(mktemp -ud %{_tmppath}/%{name}-%{version}-%{release}-XXXXXXXX)
# EFI/UEFI don't exist on PPC
ExclusiveArch: i386 x86_64 ia64

# for RHEL / Fedora when efibootmgr was part of the elilo package
Conflicts: elilo < 3.6-6
Obsoletes: elilo < 3.6-6

Source0: http://linux.dell.com/%{name}/permalink/%{name}-%{version}.tar.gz

%description
%{name} displays and allows the user to edit the Intel Extensible
Firmware Interface (EFI) Boot Manager variables.  Additional
information about EFI can be found at
http://developer.intel.com/technology/efi/efi.htm and http://uefi.org/.

%prep
%setup -q
%build
make %{?_smp_mflags} EXTRA_CFLAGS='%{optflags}'
%install
rm -rf %{buildroot}
mkdir -p %{buildroot}%{_sbindir} %{buildroot}%{_mandir}/man8
install -p --mode 755 src/%{name}/%{name} %{buildroot}%{_sbindir}
gzip -9 -c src/man/man8/%{name}.8 > src/man/man8/%{name}.8.gz
touch -r src/man/man8/%{name}.8 src/man/man8/%{name}.8.gz
install -p --mode 644 src/man/man8/%{name}.8.gz %{buildroot}%{_mandir}/man8

%clean
rm -rf %{buildroot}

%files
%defattr(-,root,root,-)
%{_sbindir}/%{name}
%{_mandir}/man8/%{name}.8.gz
%doc README INSTALL COPYING
    
%changelog
* Thu Jan  3 2008 Matt Domsch <Matt_Domsch@dell.com> 0.5.4-1
- split efibootmgr into its own RPM for Fedora/RHEL.

* Thu Aug 24 2004 Matt Domsch <Matt_Domsch@dell.com>
- new home linux.dell.com

* Fri May 18 2001 Matt Domsch <Matt_Domsch@dell.com>
- See doc/ChangeLog
