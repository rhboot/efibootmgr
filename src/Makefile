SRCDIR = $(realpath .)
TOPDIR = $(realpath ..)

include $(TOPDIR)/Make.version
include $(TOPDIR)/Make.rules
include $(TOPDIR)/Make.defaults

SUBDIR_CFLAGS = -I$(SRCDIR)/include

BINTARGETS=efibootmgr
TARGETS=$(BINTARGETS)

all : deps $(TARGETS)

EFIBOOTMGR_SOURCES = efibootmgr.c efi.c unparse_path.c
ALL_SOURCES=$(EFIBOOTMGR_SOURCES)
-include $(call deps-of,$(ALL_SOURCES))

efibootmgr : $(call objects-of,$(EFIBOOTMGR_SOURCES))
efibootmgr : PKGS=efivar efiboot

deps : $(ALL_SOURCES)
	$(MAKE) -f $(TOPDIR)/Make.deps deps SOURCES="$(ALL_SOURCES)" SUBDIR_CFLAGS="$(SUBDIR_CFLAGS)"

clean :
	@rm -rfv *.o *.a *.so $(TARGETS)
	@rm -rfv .*.d

install :
	$(INSTALL) -d -m 755 $(DESTDIR)/$(sbindir)/
	$(INSTALL) -m 755 efibootmgr $(DESTDIR)/$(sbindir)/efibootmgr
	$(INSTALL) -d -m 755 $(DESTDIR)/$(mandir)/man8/
	$(INSTALL) -m 644 efibootmgr.8 $(DESTDIR)/$(mandir)/man8/efibootmgr.8

.PHONY : all deps clean install
