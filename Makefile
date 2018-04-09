TOPDIR = $(shell echo $$PWD)

include $(TOPDIR)/Make.version
include $(TOPDIR)/Make.rules
include $(TOPDIR)/Make.defaults
include $(TOPDIR)/Make.coverity
include $(TOPDIR)/Make.scan-build

SUBDIRS := src

all install deps : | check_efidir_error Make.version
	@set -e ; for x in $(SUBDIRS) ; do \
		$(MAKE) -C $$x $@ ; \
	done

clean : | check_efidir_error Make.version
	@set -e ; for x in $(SUBDIRS) ; do \
		$(MAKE) -C $$x $@ ; \
	done

all : efibootmgr.spec

efibootmgr efibootmgr-static :
	$(MAKE) -C src $@

$(SUBDIRS) :
	$(MAKE) -C $@

.PHONY: $(SUBDIRS) 

efibootmgr.spec : | Makefile Make.version

distclean :
	$(MAKE) clean
	@rm -vf efibootmgr.spec

GITTAG = $(VERSION)

test-archive: efibootmgr.spec
	@rm -rf /tmp/efibootmgr-$(VERSION) /tmp/efibootmgr-$(VERSION)-tmp
	@mkdir -p /tmp/efibootmgr-$(VERSION)-tmp
	@git archive --format=tar $(shell git branch | awk '/^*/ { print $$2 }') | ( cd /tmp/efibootmgr-$(VERSION)-tmp/ ; tar x )
	@git diff | ( cd /tmp/efibootmgr-$(VERSION)-tmp/ ; patch -s -p1 -b -z .gitdiff )
	@mv /tmp/efibootmgr-$(VERSION)-tmp/ /tmp/efibootmgr-$(VERSION)/
	@cp efibootmgr.spec /tmp/efibootmgr-$(VERSION)/
	@dir=$$PWD; cd /tmp; tar -c --bzip2 -f $$dir/efibootmgr-$(VERSION).tar.bz2 efibootmgr-$(VERSION)
	@rm -rf /tmp/efibootmgr-$(VERSION)
	@echo "The archive is in efibootmgr-$(VERSION).tar.bz2"

tag:
	git tag -s $(GITTAG) refs/heads/master

archive: tag efibootmgr.spec
	@rm -rf /tmp/efibootmgr-$(VERSION) /tmp/efibootmgr-$(VERSION)-tmp
	@mkdir -p /tmp/efibootmgr-$(VERSION)-tmp
	@git archive --format=tar $(GITTAG) | ( cd /tmp/efibootmgr-$(VERSION)-tmp/ ; tar x )
	@mv /tmp/efibootmgr-$(VERSION)-tmp/ /tmp/efibootmgr-$(VERSION)/
	@cp efibootmgr.spec /tmp/efibootmgr-$(VERSION)/
	@dir=$$PWD; cd /tmp; tar -c --bzip2 -f $$dir/efibootmgr-$(VERSION).tar.bz2 efibootmgr-$(VERSION)
	@rm -rf /tmp/efibootmgr-$(VERSION)
	@echo "The archive is in efibootmgr-$(VERSION).tar.bz2"

