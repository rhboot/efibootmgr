
# Uncomment the lines below if we ever have subdirs to make
  #MODULES := 	
  # include the description for
  #   each module
  #include $(patsubst %, %/module.mk,$(MODULES))

# One of our parent makefiles will copy this for us...
ALLDEPS += efibootmgr_TARGETS
efibootmgr_SRCDIR := src/efibootmgr
efibootmgr_SOURCES := efibootmgr.c
efibootmgr_LIBS    := crc32.c disk.c efi.c efichar.c gpt.c scsi_ioctls.c \
                      unparse_path.c
efibootmgr_LIBDIR  := src/lib
efibootmgr_TARGETS := efibootmgr

$(efibootmgr_TARGETS): $(patsubst %,$(efibootmgr_SRCDIR)/%,$(efibootmgr_SOURCES)) $(patsubst %,$(efibootmgr_LIBDIR)/%,$(efibootmgr_LIBS))



