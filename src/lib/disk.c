/*
  disk.[ch]

  Copyright (C) 2001 Dell Computer Corporation <Matt_Domsch@dell.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "disk.h"
#include "scsi_ioctls.h"
#include "gpt.h"
#include "efibootmgr.h"

/* The major device number for virtio-blk disks is decided on module load time.
 */
static int
get_virtblk_major(void)
{
	static int cached;
	FILE *f;
	char line[256];

	if (cached != 0) {
		return cached;
	}

	cached = -1;
	f = fopen("/proc/devices", "r");
	if (f == NULL) {
		fprintf(stderr, "%s: opening /proc/devices: %s\n", __func__,
			strerror(errno));
		return cached;
	}
	while (fgets(line, sizeof line, f) != NULL) {
		size_t len = strlen(line);
		int major, scanned = 0;

		if (len == 0 || line[len - 1] != '\n') {
			break;
		}
		if (sscanf(line, "%d %n", &major, &scanned) == 1 &&
		    strcmp(line + scanned, "virtblk\n") == 0) {
			cached = major;
			break;
		}
	}
	fclose(f);
	if (cached == -1) {
		fprintf(stderr, "%s: virtio-blk driver unavailable\n",
			__func__);
	}
	return cached;
}

static int
get_nvme_major(void)
{
	static int cached;
	FILE *f;
	char line[256];

	if (cached != 0) {
		return cached;
	}

	cached = -1;
	f = fopen("/proc/devices", "r");
	if (f == NULL) {
		fprintf(stderr, "%s: opening /proc/devices: %s\n", __func__,
			strerror(errno));
		return cached;
	}
	while (fgets(line, sizeof line, f) != NULL) {
		size_t len = strlen(line);
		int major, scanned = 0;

		if (len == 0 || line[len - 1] != '\n') {
			break;
		}
		if (sscanf(line, "%d %n", &major, &scanned) == 1 &&
		    strcmp(line + scanned, "nvme\n") == 0) {
			cached = major;
			break;
		}
	}
	fclose(f);
	if (cached == -1) {
		fprintf(stderr, "%s: nvme driver unavailable\n",
			__func__);
	}
	return cached;
}

int
disk_info_from_fd(int fd, struct disk_info *info)
{
	struct stat buf;
	int rc;

	memset(info, 0, sizeof *info);
	memset(&buf, 0, sizeof(struct stat));
	rc = fstat(fd, &buf);
	if (rc == -1) {
		perror("stat");
		return 1;
	}
	if (S_ISBLK(buf.st_mode)) {
		info->major = buf.st_rdev >> 8;
		info->minor = buf.st_rdev & 0xFF;
	}
	else if (S_ISREG(buf.st_mode)) {
		info->major = buf.st_dev >> 8;
		info->minor = buf.st_dev & 0xFF;
	}
	else {
		printf("Cannot stat non-block or non-regular file\n");
		return 1;
	}

	/* IDE disks can have up to 64 partitions, or 6 bits worth,
	 * and have one bit for the disk number.
	 * This leaves an extra bit at the top.
	 */
	if (info->major == 3) {
		info->disknum = (info->minor >> 6) & 1;
		info->controllernum = (info->major - 3 + 0) + info->disknum;
		info->interface_type = ata;
		info->part    = info->minor & 0x3F;
		return 0;
	}
	else if (info->major == 22) {
		info->disknum = (info->minor >> 6) & 1;
		info->controllernum = (info->major - 22 + 2) + info->disknum;
		info->interface_type = ata;
		info->part    = info->minor & 0x3F;
		return 0;
	}
	else if (info->major >= 33 && info->major <= 34) {
		info->disknum = (info->minor >> 6) & 1;
		info->controllernum = (info->major - 33 + 4) + info->disknum;
		info->interface_type = ata;
		info->part    = info->minor & 0x3F;
		return 0;
	}
	else if (info->major >= 56 && info->major <= 57) {
		info->disknum = (info->minor >> 6) & 1;
		info->controllernum = (info->major - 56 + 8) + info->disknum;
		info->interface_type = ata;
		info->part    = info->minor & 0x3F;
		return 0;
	}
	else if (info->major >= 88 && info->major <= 91) {
		info->disknum = (info->minor >> 6) & 1;
		info->controllernum = (info->major - 88 + 12) + info->disknum;
		info->interface_type = ata;
		info->part    = info->minor & 0x3F;
		return 0;
	}

        /* I2O disks can have up to 16 partitions, or 4 bits worth. */
	if (info->major >= 80 && info->major <= 87) {
		info->interface_type = i2o;
		info->disknum = 16*(info->major-80) + (info->minor >> 4);
		info->part    = (info->minor & 0xF);
		return 0;
	}

	/* SCSI disks can have up to 16 partitions, or 4 bits worth
	 * and have one bit for the disk number.
	 */
	if (info->major == 8) {
		info->interface_type = scsi;
		info->disknum = (info->minor >> 4);
		info->part    = (info->minor & 0xF);
		return 0;
	}
	else  if ( info->major >= 65 && info->major <= 71) {
		info->interface_type = scsi;
		info->disknum = 16*(info->major-64) + (info->minor >> 4);
		info->part    = (info->minor & 0xF);
		return 0;
	}

	if (get_nvme_major() >= 0 &&
			(uint64_t)get_nvme_major() == info->major) {
		info->interface_type = nvme;
		return 0;
	}

	if (get_virtblk_major() >= 0 &&
			(uint64_t)get_virtblk_major() == info->major) {
		info->interface_type = virtblk;
		info->disknum = info->minor >> 4;
		info->part = info->minor & 0xF;
		return 0;
	}

	printf("Unknown interface type.\n");
	return 1;
}

static int
disk_get_virt_pci(const struct disk_info *info, unsigned char *bus,
		  unsigned char *device, unsigned char *function)
{
	char inbuf[32], outbuf[128];
	ssize_t lnksz;

	if (snprintf(inbuf, sizeof inbuf, "/sys/dev/block/%" PRIu64 ":%u",
		     info->major, info->minor) >= (ssize_t)(sizeof inbuf)) {
		return 1;
	}

	lnksz = readlink(inbuf, outbuf, sizeof outbuf);
	if (lnksz == -1 || lnksz == sizeof outbuf) {
		return 1;
	}

	outbuf[lnksz] = '\0';
	if (sscanf(outbuf, "../../devices/pci0000:00/0000:%hhx:%hhx.%hhx",
		   bus, device, function) != 3) {
		return 1;
	}
	return 0;
}

static int
disk_get_scsi_pci(int fd,
	     const struct disk_info *info,
	     unsigned char *bus,
	     unsigned char *device,
	     unsigned char *function)
{
	int rc, usefd=fd;
	struct stat buf;
	char slot_name[SLOT_NAME_SIZE];
	unsigned int b=0,d=0,f=0;
	memset(&buf, 0, sizeof(buf));
	rc = fstat(fd, &buf);
	if (rc == -1) {
		perror("stat");
		return 1;
	}
	if (S_ISREG(buf.st_mode)) {
		/* can't call ioctl() on this file and have it succeed.
		 * instead, need to open the block device
		 * from /dev/.
		 */
		fprintf(stderr, "You must call this program with "
			"a file name such as /dev/sda.\n");
		return 1;
	}

	rc = get_scsi_pci(usefd, slot_name, sizeof slot_name);
	if (rc) {
		perror("get_scsi_pci");
		return rc;
	}
	if (strncmp(slot_name, "virtio", 6) == 0) {
		return disk_get_virt_pci(info, bus, device, function);
	}
	rc = sscanf(slot_name, "%x:%x.%x", &b,&d,&f);
	if (rc != 3) {
		printf("sscanf failed\n");
		return 1;
	}
	*bus      = b & 0xFF;
	*device   = d & 0xFF;
	*function = f & 0xFF;
	return 0;
}

/*
 * The PCI interface treats multi-function devices as independent
 * devices.  The slot/function address of each device is encoded
 * in a single byte as follows:
 *
 *	7:3 = slot
 *	2:0 = function
 *
 *  pci bus 00 device 39 vid 8086 did 7111 channel 1
 *               00:07.1
 */
#define PCI_DEVFN(slot,func)	((((slot) & 0x1f) << 3) | ((func) & 0x07))
#define PCI_SLOT(devfn)		(((devfn) >> 3) & 0x1f)
#define PCI_FUNC(devfn)		((devfn) & 0x07)

static int
disk_get_ide_pci(int fd,
	     unsigned char *bus,
	     unsigned char *device,
	     unsigned char *function)
{
	int num_scanned, procfd;
	struct disk_info info;
	unsigned int b=0, d=0;
	char procname[80], infoline[80];
	size_t read_count __attribute__((unused));
	int rc;

	rc = disk_info_from_fd(fd, &info);
	if (rc) return rc;


	sprintf(procname, "/proc/ide/ide%d/config", info.controllernum);

	procfd = open(procname, O_RDONLY);
	if (procfd < 0) {
		perror("opening /proc/ide/ide*/config");
		return 1;
	}
	read_count = read(procfd, infoline, sizeof(infoline)-1);
	close(procfd);

	num_scanned = sscanf(infoline,
			     "pci bus %x device %x vid %*x did %*x channel %*x",
			     &b, &d);

	if (num_scanned == 2) {
		*bus      = b;
		*device   = PCI_SLOT(d);
		*function = PCI_FUNC(d);
	}
	return 0;
}

int
disk_get_pci(int fd,
	     int *interface_type,
	     unsigned char *bus,
	     unsigned char *device,
	     unsigned char *function)
{
	struct disk_info info;

	disk_info_from_fd(fd, &info);
	*interface_type = info.interface_type;

	switch (info.interface_type) {
	case ata:
		return disk_get_ide_pci(fd, bus, device, function);
		break;
	case scsi:
		return disk_get_scsi_pci(fd, &info, bus, device, function);
		break;
	case i2o:
		break;
	case md:
		break;
	case virtblk:
		return disk_get_virt_pci(&info, bus, device, function);
	default:
		break;
	}
	return 1;
}

int
disk_get_size(int fd, long *size)
{
	return ioctl(fd, BLKGETSIZE, size);
}

/**
 * is_mbr_valid(): test MBR for validity
 * @mbr: pointer to a legacy mbr structure
 *
 * Description: Returns 1 if MBR is valid, 0 otherwise.
 * Validity depends on one thing:
 *  1) MSDOS signature is in the last two bytes of the MBR
 */
static int
is_mbr_valid(legacy_mbr *mbr)
{
	if (!mbr)
		return 0;
	return (mbr->signature == MSDOS_MBR_SIGNATURE);
}

/************************************************************
 * msdos_disk_get_extended partition_info()
 * Requires:
 *  - open file descriptor fd
 *  - start, size
 * Modifies: all these
 * Returns:
 *  0 on success
 *  non-zero on failure
 *
 ************************************************************/

static int
msdos_disk_get_extended_partition_info (int fd, legacy_mbr *mbr,
					uint32_t num,
					uint64_t *start, uint64_t *size)
{
        /* Until I can handle these... */
        fprintf(stderr, "Extended partition info not supported.\n");
        return 1;
}

/************************************************************
 * msdos_disk_get_partition_info()
 * Requires:
 *  - mbr
 *  - open file descriptor fd (for extended partitions)
 *  - start, size, signature, mbr_type, signature_type
 * Modifies: all these
 * Returns:
 *  0 on success
 *  non-zero on failure
 *
 ************************************************************/

static int
msdos_disk_get_partition_info (int fd, legacy_mbr *mbr,
			       uint32_t num,
			       uint64_t *start, uint64_t *size,
			       char *signature,
			       uint8_t *mbr_type, uint8_t *signature_type)
{
	int rc;
	long disk_size=0;
	struct stat stat;
	struct timeval tv;

	if (!mbr) return 1;
	if (!is_mbr_valid(mbr)) return 1;

	*mbr_type = 0x01;
	*signature_type = 0x01;

	if (!mbr->unique_mbr_signature && !opts.write_signature) {
		printf("\n\n******************************************************\n");
		printf("Warning! This MBR disk does not have a unique signature.\n");
		printf("If this is not the first disk found by EFI, you may not be able\n");
		printf("to boot from it without a unique signature.\n");
		printf("Run efibootmgr with the -w flag to write a unique signature\n");
		printf("to the disk.\n");
		printf("******************************************************\n\n");
	}
	else if (!mbr->unique_mbr_signature && opts.write_signature) {
		/* MBR Signatures must be unique for the
		   EFI Boot Manager
		   to find the right disk to boot from */
		rc = fstat(fd, &stat);
		if (rc == -1) {
			perror("stat disk");
		}

		rc = gettimeofday(&tv, NULL);
		if (rc == -1) {
			perror("gettimeofday");
		}

		/* Write the device type to the signature.
		   This should be unique per disk per system */
		mbr->unique_mbr_signature =  tv.tv_usec << 16;
		mbr->unique_mbr_signature |= stat.st_rdev & 0xFFFF;

		/* Write it to the disk */
		lseek(fd, 0, SEEK_SET);
		rc = write(fd, mbr, sizeof(*mbr));
	}
	*(uint32_t *)signature = mbr->unique_mbr_signature;

        if (num > 4) {
		/* Extended partition */
                return msdos_disk_get_extended_partition_info(fd, mbr, num,
                                                              start, size);
        }
	else if (num == 0) {
		/* Whole disk */
                *start = 0;
		disk_get_size(fd, &disk_size);
                *size = disk_size;
	}
	else if (num >= 1 && num <= 4) {
		/* Primary partition */
                *start = mbr->partition[num-1].starting_lba;
                *size  = mbr->partition[num-1].size_in_lba;
	}
	return 0;
}

/************************************************************
 * get_sector_size
 * Requires:
 *  - filedes is an open file descriptor, suitable for reading
 * Modifies: nothing
 * Returns:
 *  sector size, or 512.
 ************************************************************/
int
get_sector_size(int filedes)
{
	int rc, sector_size = 512;

	rc = ioctl(filedes, BLKSSZGET, &sector_size);
	if (rc)
		sector_size = 512;
	return sector_size;
}

/************************************************************
 * lcm
 * Requires:
 * - numbers of which to find the lowest common multiple
 * Modifies: nothing
 * Returns:
 *  lowest common multiple of x and y
 ************************************************************/
unsigned int
lcm(unsigned int x, unsigned int y)
{
	unsigned int m = x, n = y, o;

	while ((o = m % n)) {
		m = n;
		n = o;
	}

	return (x / n) * y;
}

/**
 * disk_get_partition_info()
 *  @fd - open file descriptor to disk
 *  @num   - partition number (1 is first partition on the disk)
 *  @start - partition starting sector returned
 *  @size  - partition size (in sectors) returned
 *  @signature - partition signature returned
 *  @mbr_type  - partition type returned
 *  @signature_type - signature type returned
 *
 *  Description: Finds partition table info for given partition on given disk.
 *               Both GPT and MSDOS partition tables are tested for.
 *  Returns 0 on success, non-zero on failure
 */
int
disk_get_partition_info (int fd,
			 uint32_t num,
			 uint64_t *start, uint64_t *size,
			 char *signature,
			 uint8_t *mbr_type, uint8_t *signature_type)
{
	legacy_mbr *mbr;
	void *mbr_sector;
	size_t mbr_size;
	off_t offset __attribute__((unused));
	int this_bytes_read = 0;
	int gpt_invalid=0, mbr_invalid=0;
	int rc=0;
	int sector_size = get_sector_size(fd);


	mbr_size = lcm(sizeof(*mbr), sector_size);
	if ((rc = posix_memalign(&mbr_sector, sector_size, mbr_size)) != 0)
		goto error;
	memset(mbr_sector, '\0', mbr_size);

	offset = lseek(fd, 0, SEEK_SET);
	this_bytes_read = read(fd, mbr_sector, mbr_size);
	if (this_bytes_read < (ssize_t)sizeof(*mbr)) {
		rc=1;
		goto error_free_mbr;
	}
	mbr = (legacy_mbr *)mbr_sector;
	gpt_invalid = gpt_disk_get_partition_info(fd, num,
						  start, size,
						  signature,
						  mbr_type,
						  signature_type);
	if (gpt_invalid) {
		mbr_invalid = msdos_disk_get_partition_info(fd, mbr, num,
							    start, size,
							    signature,
							    mbr_type,
							    signature_type);
		if (mbr_invalid) {
			rc=1;
			goto error_free_mbr;
		}
	}
 error_free_mbr:
	free(mbr_sector);
 error:
	return rc;
}

#ifdef DISK_EXE
int
main (int argc, char *argv[])
{
	int fd, rc, interface_type;
	unsigned char bus=0,device=0,func=0;
	if (argc <= 1) return 1;
	fd = open(argv[1], O_RDONLY|O_DIRECT);
	rc = disk_get_pci(fd, &interface_type, &bus, &device, &func);
	if (!rc) {
		printf("PCI %02x:%02x.%02x\n", bus, device, func);
	}
	return rc;
}
#endif
