efibootmgr_SRCDIR := src/efibootmgr
efibootmgr_OBJECTS := efibootmgr.o
efibootmgr_TARGETS := efibootmgr

efibootmgr_LIBS    := crc32.o disk.o efi.o efichar.o gpt.o scsi_ioctls.o \
                      unparse_path.o
efibootmgr_LIBDIR  := src/lib

ALLDEPS += $(patsubst %, $(efibootmgr_SRCDIR)/%, $(efibootmgr_TARGETS))
CLEANLIST += $(patsubst %, $(efibootmgr_SRCDIR)/%, $(efibootmgr_TARGETS))
CLEANLIST += $(patsubst %, $(efibootmgr_SRCDIR)/%, $(efibootmgr_OBJECTS))

CFLAGS += -Isrc/include -Isrc/lib

$(patsubst %, $(efibootmgr_SRCDIR)/%, $(efibootmgr_TARGETS)):  \
  	$(patsubst %,$(efibootmgr_SRCDIR)/%,$(efibootmgr_OBJECTS)) \
  	$(patsubst %,$(efibootmgr_LIBDIR)/%,$(efibootmgr_LIBS))
