  default: all

  RELEASE_MAJOR := 0
  RELEASE_MINOR := 2
  RELEASE_SUBLEVEL := 0
  RELEASE_EXTRALEVEL := 
  RELEASE_STRING := efibootmgr-$(RELEASE_MAJOR).$(RELEASE_MINOR).$(RELEASE_SUBLEVEL)$(RELEASE_EXTRALEVEL)

  BUILDDIR := $(shell pwd)

  MODULES := src

#Included makefiles will add their deps for each stage in these vars:
  INSTALLDEPS :=
  CLEANDEPS :=
  ALLDEPS :=

#Include make rules from each submodule (subdirectory)
  include $(patsubst %,%/module.mk,$(MODULES))

  .PHONY: all clean install install_link post_install echotree default

  all:  $(ALLDEPS) 
  clean: $(CLEANDEPS) 

  install_list: echotree $(INSTALLDEPS) 

  install: all 
	@make install_list | package/install.pl copy
	@make post_install

  install_link: all
	@make install_list | package/install.pl link
	@make post_install

  tarball: clean
	-rm ali-*.tar.gz
	cp -a ../ali-dist ../$(RELEASE_STRING)
	find ../$(RELEASE_STRING) -name CVS -type d -depth -exec rm -rf {} \;
	cd ..; tar cvzf $(RELEASE_STRING).tar.gz $(RELEASE_STRING)
	mv ../$(RELEASE_STRING).tar.gz .
	rm -rf ../$(RELEASE_STRING)


#The rest of the docs...
  doc_TARGETS += COPYING TODO


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
