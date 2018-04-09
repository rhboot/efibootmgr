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

GITTAG = $(shell bash -c "echo $$(($(VERSION) + 1))")

test-archive: efibootmgr.spec
	@rm -rf /tmp/efibootmgr-$(GITTAG) /tmp/efibootmgr-$(GITTAG)-tmp
	@mkdir -p /tmp/efibootmgr-$(GITTAG)-tmp
	@git archive --format=tar $(shell git branch | awk '/^*/ { print $$2 }') | ( cd /tmp/efibootmgr-$(GITTAG)-tmp/ ; tar x )
	@git diff | ( cd /tmp/efibootmgr-$(GITTAG)-tmp/ ; patch -s -p1 -b -z .gitdiff )
	@mv /tmp/efibootmgr-$(GITTAG)-tmp/ /tmp/efibootmgr-$(GITTAG)/
	@cp efibootmgr.spec /tmp/efibootmgr-$(GITTAG)/
	@dir=$$PWD; cd /tmp; tar -c --bzip2 -f $$dir/efibootmgr-$(GITTAG).tar.bz2 efibootmgr-$(GITTAG)
	@rm -rf /tmp/efibootmgr-$(GITTAG)
	@echo "The archive is in efibootmgr-$(GITTAG).tar.bz2"

bumpver :
	@echo VERSION=$(GITTAG) > Make.version
	@git add Make.version
	git commit -m "Bump version to $(GITTAG)" -s

tag:
	git tag -s $(GITTAG) refs/heads/master

archive: bumpver tag efibootmgr.spec
	@rm -rf /tmp/efibootmgr-$(GITTAG) /tmp/efibootmgr-$(GITTAG)-tmp
	@mkdir -p /tmp/efibootmgr-$(GITTAG)-tmp
	@git archive --format=tar $(GITTAG) | ( cd /tmp/efibootmgr-$(GITTAG)-tmp/ ; tar x )
	@mv /tmp/efibootmgr-$(GITTAG)-tmp/ /tmp/efibootmgr-$(GITTAG)/
	@cp efibootmgr.spec /tmp/efibootmgr-$(GITTAG)/
	@dir=$$PWD; cd /tmp; tar -c --bzip2 -f $$dir/efibootmgr-$(GITTAG).tar.bz2 efibootmgr-$(GITTAG)
	@rm -rf /tmp/efibootmgr-$(GITTAG)
	@echo "The archive is in efibootmgr-$(GITTAG).tar.bz2"

