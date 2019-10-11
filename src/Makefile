SRCDIR = $(realpath .)
TOPDIR = $(realpath ..)

include $(TOPDIR)/Make.version
include $(TOPDIR)/Make.rules
include $(TOPDIR)/Make.defaults

SUBDIR_CFLAGS = -I$(SRCDIR)/include

BINTARGETS=efibootmgr efibootdump
TARGETS=$(BINTARGETS) efibootmgr.8 efibootdump.8

all : deps $(TARGETS)

EFIBOOTMGR_SOURCES = efibootmgr.c efi.c parse_loader_data.c
EFICONMAN_SOURCES = eficonman.c
EFIBOOTDUMP_SOURCES = efibootdump.c parse_loader_data.c
EFIBOOTNEXT_SOURCES = efibootnext.c
ALL_SOURCES=$(EFIBOOTMGR_SOURCES)
-include $(call deps-of,$(ALL_SOURCES))

efibootmgr : $(call objects-of,$(EFIBOOTMGR_SOURCES))
efibootmgr : PKGS=efivar efiboot

eficonman : $(call objects-of,$(EFICONMAN_SOURCES))
eficonman : PKGS=efivar efiboot popt

efibootdump : $(call objects-of,$(EFIBOOTDUMP_SOURCES))
efibootdump : PKGS=efivar efiboot popt

efibootnext : $(call objects-of,$(EFIBOOTNEXT_SOURCES))
efibootnext : PKGS=efivar efiboot popt

deps : PKGS=efivar efiboot popt
deps : $(ALL_SOURCES)
	$(MAKE) -f $(TOPDIR)/Make.deps \
		SOURCES="$(ALL_SOURCES)" \
		SUBDIR_CFLAGS="$(SUBDIR_CFLAGS)" \
		PKGS="$(PKGS)" \
		deps

clean :
	@rm -rfv *.o *.a *.so $(TARGETS)
	@rm -rfv .*.d

install : $(TARGETS)
	$(INSTALL) -d -m 755 $(DESTDIR)/$(sbindir)/
	$(INSTALL) -m 755 efibootmgr $(DESTDIR)/$(sbindir)/efibootmgr
	$(INSTALL) -m 755 efibootdump $(DESTDIR)/$(sbindir)/efibootdump
	$(INSTALL) -d -m 755 $(DESTDIR)/$(mandir)/man8/
	$(INSTALL) -m 644 efibootmgr.8 $(DESTDIR)/$(mandir)/man8/efibootmgr.8
	$(INSTALL) -m 644 efibootdump.8 $(DESTDIR)/$(mandir)/man8/efibootdump.8

.PHONY : all deps clean install
