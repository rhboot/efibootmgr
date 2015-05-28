  default: all

  SIGNING_KEY := pjones
  RELEASE_MAJOR := 0
  RELEASE_MINOR := 11
  RELEASE_NAME := efibootmgr
  VERSION := $(RELEASE_MAJOR).$(RELEASE_MINOR)
  RELEASE_STRING := $(RELEASE_NAME)-$(RELEASE_MAJOR).$(RELEASE_MINOR)

  CFLAGS = $(EXTRA_CFLAGS) -DEFIBOOTMGR_VERSION=\"$(RELEASE_MAJOR).$(RELEASE_MINOR)\" \
	    -Wsign-compare -Wall -Werror -g -D_FILE_OFFSET_BITS=64 \
	    -I/usr/include/efivar

  MODULES := src

  BINDIR := /usr/sbin

#--------------------------------------------
# Generic Makefile stuff is below. You
#  should not have to modify any of the stuff
#  below.
#--------------------------------------------

#Included makefiles will add their deps for each stage in these vars:
  INSTALLDEPS :=
  CLEANDEPS :=
  ALLDEPS := efibootmgr.spec
  CLEANLIST := efibootmgr.spec

#Define the top-level build directory
  BUILDDIR := $(shell pwd)

#Include make rules from each submodule (subdirectory)
  include $(patsubst %,%/module.mk,$(MODULES))

  .PHONY: all clean install_list install install_link post_install tarball echotree default tag archive test-archive

  all:  $(ALLDEPS)
  clean: clean_list $(CLEANDEPS) 

  clean_list:
	rm -f $(CLEANLIST)

  install_list: echotree $(INSTALLDEPS) 

  install: all 
	@make install_list | tools/install.pl copy

  install_link: all
	@make install_list | tools/install.pl link

  post_install: 

GITTAG = $(RELEASE_STRING)

efibootmgr.spec : efibootmgr.spec.in Makefile
	@sed -e "s,@@VERSION@@,$(VERSION),g" $< > $@

test-archive: efibootmgr.spec
	@rm -rf /tmp/$(RELEASE_STRING) /tmp/$(RELEASE_STRING)-tmp
	@mkdir -p /tmp/$(RELEASE_STRING)-tmp
	@git archive --format=tar $(shell git branch | awk '/^*/ { print $$2 }') | ( cd /tmp/$(RELEASE_STRING)-tmp/ ; tar x )
	@git diff | ( cd /tmp/$(RELEASE_STRING)-tmp/ ; patch -s -p1 -b -z .gitdiff )
	@mv /tmp/$(RELEASE_STRING)-tmp/ /tmp/$(RELEASE_STRING)/
	@cp efibootmgr.spec /tmp/$(RELEASE_STRING)/
	@dir=$$PWD; cd /tmp; tar -c --bzip2 -f $$dir/$(RELEASE_STRING).tar.bz2 $(RELEASE_STRING)
	@rm -rf /tmp/$(RELEASE_STRING)
	@echo "The archive is in $(RELEASE_STRING).tar.bz2"

tag:
	git tag -s $(GITTAG) refs/heads/master

archive: tag efibootmgr.spec
	@rm -rf /tmp/$(RELEASE_STRING) /tmp/$(RELEASE_STRING)-tmp
	@mkdir -p /tmp/$(RELEASE_STRING)-tmp
	@git archive --format=tar $(GITTAG) | ( cd /tmp/$(RELEASE_STRING)-tmp/ ; tar x )
	@mv /tmp/$(RELEASE_STRING)-tmp/ /tmp/$(RELEASE_STRING)/
	@cp efibootmgr.spec /tmp/$(RELEASE_STRING)/
	@dir=$$PWD; cd /tmp; tar -c --bzip2 -f $$dir/$(RELEASE_STRING).tar.bz2 $(RELEASE_STRING)
	@rm -rf /tmp/$(RELEASE_STRING)
	@echo "The archive is in $(RELEASE_STRING).tar.bz2"

tarball: archive

#The rest of the docs...
  doc_TARGETS += COPYING README INSTALL

echotree:
	@# making directory tree 
	@#RPM FORMAT:
	@# %defattr(-, user, group) 
	@# %attr(4755,user,group)  filename
	@# filename

# Here is a list of variables that are assumed Local to each Makefile. You can
#   safely stomp on these values without affecting the build.
# 	MODULES
#	FILES
#	TARGETS
#	SOURCES
