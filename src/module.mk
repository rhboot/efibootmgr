#add our stuff first... our children need to wait for us to finish
INSTALLDEPS += bindir_INSTALL

  MODULES := src/efibootmgr src/lib src/include
  include $(patsubst %,%/module.mk,$(MODULES))

# MAKE SURE TO DEFINE $(BINDIR) SOMEWHERE BEFORE USING THE 
# CODE BELOW!!!
# Common stuff to copy into the common directories
bindir_INSTALL:    
	@echo "R-M-: %attr(0755,root,ali) $(BINDIR)"
	@for file in $(bindir_TARGETS) ;\
	do                                              \
	  echo "-C--: $(BUILDDIR)/$$file $(BINDIR)/"                ;\
	  echo "R---: %attr(0755,root,ali) $(BINDIR)/`basename $$file`" ;\
	done
	@echo 

