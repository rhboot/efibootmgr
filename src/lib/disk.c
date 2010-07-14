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
#include <stdint.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include "disk.h"
#include "scsi_ioctls.h"
#include "gpt.h"
#include "efibootmgr.h"

#define BLKSSZGET  _IO(0x12,104)	/* get block device sector size */

int
disk_info_from_fd(int fd, 
		  int *interface_type,
		  unsigned int *controllernum, 
		  unsigned int *disknum,
		  unsigned char *part)
{
	struct stat buf;
	int rc;
	uint64_t major;
	unsigned char minor;
	memset(&buf, 0, sizeof(struct stat));
	rc = fstat(fd, &buf);
	if (rc == -1) {
		perror("stat");
		return 1;
	}
	if (!(S_ISBLK(buf.st_mode) || S_ISREG(buf.st_mode))) {
		printf("Cannot stat non-block or non-regular file\n");
		return 1;
	}
	major = buf.st_dev >> 8;
	minor = buf.st_dev & 0xFF;

	/* IDE disks can have up to 64 partitions, or 6 bits worth,
	 * and have one bit for the disk number.
	 * This leaves an extra bit at the top.
	 */
	if (major == 3) {
		*disknum = (minor >> 6) & 1;
		*controllernum = (major - 3 + 0) + *disknum;
		*interface_type = ata;
		*part    = minor & 0x3F;
		return 0;
	}
	else if (major == 22) {
		*disknum = (minor >> 6) & 1;
		*controllernum = (major - 22 + 2) + *disknum;
		*interface_type = ata;
		*part    = minor & 0x3F;
		return 0;
	}
	else if (major >= 33 && major <= 34) {
		*disknum = (minor >> 6) & 1;
		*controllernum = (major - 33 + 4) + *disknum;
		*interface_type = ata;
		*part    = minor & 0x3F;
		return 0;
	}
	else if (major >= 56 && major <= 57) {
		*disknum = (minor >> 6) & 1;
		*controllernum = (major - 56 + 8) + *disknum;
		*interface_type = ata;
		*part    = minor & 0x3F;
		return 0;
	}
	else if (major >= 88 && major <= 91) {
		*disknum = (minor >> 6) & 1;
		*controllernum = (major - 88 + 12) + *disknum;
		*interface_type = ata;
		*part    = minor & 0x3F;
		return 0;
	}
 	
        /* I2O disks can have up to 16 partitions, or 4 bits worth. */
	if (major >= 80 && major <= 87) {
		*interface_type = i2o;
		*disknum = 16*(major-80) + (minor >> 4);
		*part    = (minor & 0xF);
		return 0;
	}

	/* SCSI disks can have up to 16 partitions, or 4 bits worth
	 * and have one bit for the disk number.
	 */
	if (major == 8) {
		*interface_type = scsi;
		*disknum = (minor >> 4);
		*part    = (minor & 0xF);
		return 0;
	}
	else  if ( major >= 65 && major <= 71) {
		*interface_type = scsi;
		*disknum = 16*(major-64) + (minor >> 4);
		*part    = (minor & 0xF);
		return 0;
	}
	    
	printf("Unknown interface type.\n");
	return 1;
}

static int
disk_get_scsi_pci(int fd, 
	     unsigned char *bus,
	     unsigned char *device,
	     unsigned char *function)
{
	int rc, usefd=fd;
	struct stat buf;
	char slot_name[8];
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

	rc = get_scsi_pci(usefd, slot_name);
	if (rc) {
		perror("get_scsi_pci");
		return rc;
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
	unsigned int b=0,d=0,disknum=0, controllernum=0;
	unsigned char part=0;
	char procname[80], infoline[80];
	size_t read_count;
	int interface_type;
	int rc;
	
	rc = disk_info_from_fd(fd, &interface_type, &controllernum,
			       &disknum, &part);
	if (rc) return rc;


	sprintf(procname, "/proc/ide/ide%d/config", controllernum);
	
	procfd = open(procname, O_RDONLY);
	if (!procfd) {
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



#if 0
/* this is a list of devices */
static int
disk_get_md_parts(int fd)
{
	return 0;
}
#endif

int
disk_get_pci(int fd,
	     unsigned char *bus,
	     unsigned char *device,
	     unsigned char *function)
{
	int interface_type=interface_type_unknown;
	unsigned int controllernum=0, disknum=0;
	unsigned char part=0;
	
	disk_info_from_fd(fd,
			  &interface_type,
			  &controllernum, 
			  &disknum,
			  &part);
	switch (interface_type) {
	case ata:
		return disk_get_ide_pci(fd, bus, device, function);
		break;
	case scsi:
		return disk_get_scsi_pci(fd, bus, device, function);
		break;
	case i2o:
		break;
	case md:
		break;
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
	else if (opts.write_signature) {
		
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
		write(fd, mbr, sizeof(*mbr));

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
	off_t offset;
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
	if (this_bytes_read < sizeof(*mbr)) {
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
	int fd, rc;
	unsigned char bus=0,device=0,func=0;
	if (argc <= 1) return 1;
	fd = open(argv[1], O_RDONLY|O_DIRECT);
	rc = disk_get_pci(fd, &bus, &device, &func);
	if (!rc) {
		printf("PCI %02x:%02x.%02x\n", bus, device, func);
	}
	return rc;
}
#endif
