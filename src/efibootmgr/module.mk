efibootmgr_SRCDIR := src/efibootmgr
efibootmgr_OBJECTS := efibootmgr.o
efibootmgr_TARGETS := efibootmgr
efibootmgr_FULLTARGET :=  \
	$(patsubst %, $(efibootmgr_SRCDIR)/%, $(efibootmgr_TARGETS))
efibootmgr_FULLOBJECT :=  \
	$(patsubst %, $(efibootmgr_SRCDIR)/%, $(efibootmgr_OBJECT))

efibootmgr_LIBS    := crc32.o disk.o efi.o efichar.o gpt.o scsi_ioctls.o \
                      unparse_path.o efivars_procfs.o efivars_sysfs.o
efibootmgr_LIBDIR  := src/lib
efibootmgr_FULLLIB := \
	$(patsubst %,$(efibootmgr_LIBDIR)/%,$(efibootmgr_LIBS))
LIBS = -lpci

ALLDEPS += $(efibootmgr_FULLTARGET)
CLEANLIST += $(efibootmgr_FULLTARGET)
CLEANLIST += $(efibootmgr_FULLOBJECT)
bindir_TARGETS += $(efibootmgr_FULLTARGET)

$(efibootmgr_FULLTARGET): \
	$(efibootmgr_FULLOBJECT) \
	$(efibootmgr_FULLLIB)
	$(CC) $(CFLAGS) $(LDFLAGS) $(efibootmgr_SRCDIR)/efibootmgr.c $^ $(LIBS) -o $@
