#add our stuff first... our children need to wait for us to finish
INSTALLDEPS += bindir_LIST

  MODULES := src/efibootmgr src/lib src/include
  include $(patsubst %,%/module.mk,$(MODULES))


# Common stuff to copy into the common directories
#  Note that the stuff below bindir_LIST is all on one line...
bindir_LIST:    
	@if [ ! -z "$(bindir_TARGETS)" ]; then \
	  echo "R-M-: %attr(0755,root,root) $(BINDIR)" ;\
	  for file in $(bindir_TARGETS) ;\
	  do                                              \
	    echo "-C--: $(BUILDDIR)/$$file $(BINDIR)/"                ;\
	    echo "R---: %attr(0755,root,root) $(BINDIR)/`basename $$file`" ;\
	  done ;\
	  echo  ;\
	fi


