TOPDIR = $(shell echo $$PWD)

include $(TOPDIR)/Make.version
include $(TOPDIR)/Make.rules
include $(TOPDIR)/Make.defaults

SUBDIRS := src

all install deps : | check_efidir_error Make.version
	@set -e ; for x in $(SUBDIRS) ; do \
		$(MAKE) -C $$x $@ ; \
	done

clean : | check_efidir_error Make.version
	@set -e ; for x in $(SUBDIRS) ; do \
		$(MAKE) -C $$x $@ ; \
	done
	@rm -rvf cov-int efibootmgr-coverity-*.tar.*

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

COV_EMAIL=$(call get-config,coverity.email)
COV_TOKEN=$(call get-config,coverity.token)
COV_URL=$(call get-config,coverity.url)
COV_FILE=efibootmgr-coverity-$(VERSION)-$(COMMIT_ID).tar.bz2
COMMIT_ID=$(shell git log -1 --pretty=%H 2>/dev/null || echo master)

cov-int : clean
	cov-build --dir cov-int make all

$(COV_FILE) : cov-int
	tar caf $@ cov-int

cov-upload :
	@if [[ -n "$(COV_URL)" ]] &&					\
	    [[ -n "$(COV_TOKEN)" ]] &&					\
	    [[ -n "$(COV_EMAIL)" ]] ;					\
	then								\
		echo curl --form token=$(COV_TOKEN) --form email="$(COV_EMAIL)" --form file=@"$(COV_FILE)" --form version=$(VERSION).1 --form description="$(COMMIT_ID)" "$(COV_URL)" ; \
		curl --form token=$(COV_TOKEN) --form email="$(COV_EMAIL)" --form file=@"$(COV_FILE)" --form version=$(VERSION).1 --form description="$(COMMIT_ID)" "$(COV_URL)" ; \
	else								\
		echo Coverity output is in $(COV_FILE) ;		\
	fi

coverity : $(COV_FILE) cov-upload

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

