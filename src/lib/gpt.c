/*
    gpt.[ch]

    Copyright (C) 2000-2001 Dell Computer Corporation <Matt_Domsch@dell.com> 

    EFI GUID Partition Table handling
    Per Intel EFI Specification v1.02
    http://developer.intel.com/technology/efi/efi.htm

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

/* For PRIx64 */
#define __STDC_FORMAT_MACROS

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "crc32.h"
#include "gpt.h"
#include "efibootmgr.h"

#define BLKGETSIZE _IO(0x12,96)	/* return device size */
#define BLKSSZGET  _IO(0x12,104)	/* get block device sector size */
#define BLKGETSIZE64 _IOR(0x12,114,sizeof(uint64_t))	/* return device size in bytes (u64 *arg) */

/************************************************************
 * efi_crc32()
 * Requires:
 *  - a buffer of length len
 * Modifies: nothing
 * Returns:
 *  EFI-style CRC32 value for buf
 *  
 * This function uses the crc32 function by Gary S. Brown,
 * but seeds the function with ~0, and xor's with ~0 at the end.
 ************************************************************/

static inline uint32_t
efi_crc32(const void *buf, unsigned long len)
{
	return (crc32(buf, len, ~0L) ^ ~0L);
}

static inline int
IsLegacyMBRValid(legacy_mbr * mbr)
{
	return (mbr ? (mbr->signature == MSDOS_MBR_SIGNATURE) : 0);
}

static inline int
efi_guidcmp(efi_guid_t left, efi_guid_t right)
{
	return memcmp(&left, &right, sizeof (efi_guid_t));
}

/************************************************************
 * get_sector_size
 * Requires:
 *  - filedes is an open file descriptor, suitable for reading
 * Modifies: nothing
 * Returns:
 *  sector size, or 512.
 ************************************************************/
static int
get_sector_size(int filedes)
{
	int rc, sector_size = 512;

	rc = ioctl(filedes, BLKSSZGET, &sector_size);
	if (rc)
		sector_size = 512;
	return sector_size;
}

/************************************************************
 * _get_num_sectors
 * Requires:
 *  - filedes is an open file descriptor, suitable for reading
 * Modifies: nothing
 * Returns:
 *  Last LBA value on success 
 *  0 on error
 *
 * Try getting BLKGETSIZE64 and BLKSSZGET first,
 * then BLKGETSIZE if necessary.
 ************************************************************/
static uint64_t
_get_num_sectors(int filedes)
{
	uint64_t sectors, size_in_bytes = 0;
	int rc;

	rc = ioctl(filedes, BLKGETSIZE64, &size_in_bytes);
	if (!rc)
		return size_in_bytes / get_sector_size(filedes);
	else {
		rc = ioctl(filedes, BLKGETSIZE, &sectors);
		if (rc)
			return 0;
	}
	return sectors;
}

/************************************************************
 * LastLBA()
 * Requires:
 *  - filedes is an open file descriptor, suitable for reading
 * Modifies: nothing
 * Returns:
 *  Last LBA value on success 
 *  0 on error
 * Notes: The value st_blocks gives the size of the file
 *        in 512-byte blocks, which is OK if
 *        EFI_BLOCK_SIZE_SHIFT == 9.
 ************************************************************/

static uint64_t
LastLBA(int filedes)
{
	int rc;
	uint64_t sectors = 0;
	struct stat s;
	memset(&s, 0, sizeof (s));
	rc = fstat(filedes, &s);
	if (rc == -1) {
		fprintf(stderr, "LastLBA() could not stat: %s\n",
			strerror(errno));
		return 0;
	}

	if (S_ISBLK(s.st_mode)) {
		sectors = _get_num_sectors(filedes);
	} else {
		fprintf(stderr,
			"LastLBA(): I don't know how to handle files with mode %x\n",
			s.st_mode);
		sectors = 1;
	}

	return sectors - 1;
}

/************************************************************
 * IsLBAValid()
 * Requires:
 *  - fd
 *  - lba is the logical block address desired
 * Modifies: nothing
 * Returns:
 * - 1 if true
 * - 0 if false
 ************************************************************/

static inline int
IsLBAValid(int fd, uint64_t lba)
{
	return (lba <= LastLBA(fd));
}

static int
ReadLBA(int fd, uint64_t lba, void *buffer, size_t bytes)
{
	int sector_size = get_sector_size(fd);
	off_t offset = lba * sector_size;

	lseek(fd, offset, SEEK_SET);
	return read(fd, buffer, bytes);
}

/************************************************************
 * ReadGuidPartitionEntries()
 * Requires:
 *  - fd
 *  - lba is the Logical Block Address of the partition table
 *  - gpt is a buffer into which the GPT will be put  
 * Modifies:
 *  - fd
 *  - gpt
 * Returns:
 *   pte on success
 *   NULL on error
 * Notes: remember to free pte when you're done!
 ************************************************************/
static gpt_entry *
ReadGuidPartitionEntries(int fd, gpt_header * gpt)
{
	gpt_entry *pte;

	pte = (gpt_entry *)
	    malloc(gpt->num_partition_entries * gpt->sizeof_partition_entry);

	if (!pte)
		return NULL;

	memset(pte, 0, gpt->num_partition_entries *
	       gpt->sizeof_partition_entry);

	if (!ReadLBA(fd, gpt->partition_entry_lba, pte,
		     gpt->num_partition_entries *
		     gpt->sizeof_partition_entry)) {
		free(pte);
		return NULL;
	}
	return pte;
}

/************************************************************
 * ReadGuidPartitionTableHeader()
 * Requires:
 *  - lba is the Logical Block Address of the partition table
 * Modifies:
 *  - fd
 * Returns:
 *   GPTH on success
 *   NULL on error
 ************************************************************/
static gpt_header *
ReadGuidPartitionTableHeader(int fd, uint64_t lba)
{
	gpt_header *gpt;
	gpt = (gpt_header *)
	    malloc(sizeof (gpt_header));
	if (!gpt)
		return NULL;
	memset(gpt, 0, sizeof (*gpt));
	if (!ReadLBA(fd, lba, gpt, sizeof (gpt_header))) {
		free(gpt);
		return NULL;
	}

	return gpt;
}

/************************************************************
 * IsGuidPartitionTableValid()
 * Requires:
 *  - fd
 *  - lba is the Logical Block Address of the partition table
 * Modifies:
 *  - fd
 *  - gpt  - reads data into gpt
 *  - ptes - reads data into ptes
 * Returns:
 *   1 if valid
 *   0 on error
 ************************************************************/
static int
IsGuidPartitionTableValid(int fd, uint64_t lba,
			  gpt_header ** gpt, gpt_entry ** ptes)
{
	int rc = 0;		/* default to not valid */
	uint32_t crc, origcrc;

	if (!gpt || !ptes)
		return rc;

	// printf("IsGuidPartitionTableValid(%llx)\n", lba);
	if (!(*gpt = ReadGuidPartitionTableHeader(fd, lba)))
		return rc;
	/* Check the GUID Partition Table signature */
	if ((*gpt)->signature != GPT_HEADER_SIGNATURE) {
		/* 
		   printf("GUID Partition Table Header signature is wrong: %llx != %llx\n",
		   (*gpt)->signature, GUID_PT_HEADER_SIGNATURE);
		 */
		free(*gpt);
		*gpt = NULL;
		return rc;
	}

	/* Check the GUID Partition Table Header CRC */
	origcrc = (*gpt)->header_crc32;
	(*gpt)->header_crc32 = 0;
	crc = efi_crc32(*gpt, (*gpt)->header_size);
	if (crc != origcrc) {
		// printf( "GPTH CRC check failed, %x != %x.\n", origcrc, crc);
		(*gpt)->header_crc32 = origcrc;
		free(*gpt);
		*gpt = NULL;
		return rc;
	}
	(*gpt)->header_crc32 = origcrc;
	/* Check that the my_lba entry points to the LBA
	   that contains the GPT we read */
	if ((*gpt)->my_lba != lba) {
		// printf( "my_lba %llx != lba %llx.\n", (*gpt)->my_lba, lba);
		free(*gpt);
		*gpt = NULL;
		return rc;
	}

	if (!(*ptes = ReadGuidPartitionEntries(fd, *gpt))) {
		free(*gpt);
		*gpt = NULL;
		return rc;
	}

	/* Check the GUID Partition Entry Array CRC */
	crc = efi_crc32(*ptes, (*gpt)->num_partition_entries *
			(*gpt)->sizeof_partition_entry);
	if (crc != (*gpt)->partition_entry_array_crc32) {
		// printf("GUID Partitition Entry Array CRC check failed.\n");
		free(*gpt);
		*gpt = NULL;
		free(*ptes);
		*ptes = NULL;
		return rc;
	}

	/* We're done, all's well */
	return 1;
}

/**
 * is_pmbr_valid(): test Protective MBR for validity
 * @mbr: pointer to a legacy mbr structure
 *
 * Description: Returns 1 if PMBR is valid, 0 otherwise.
 * Validity depends on two things:
 *  1) MSDOS signature is in the last two bytes of the MBR
 *  2) One partition of type 0xEE is found
 */
static int
is_pmbr_valid(legacy_mbr *mbr)
{
	int i, found = 0, signature = 0;
	if (!mbr)
		return 0;
	signature = (mbr->signature == MSDOS_MBR_SIGNATURE);
	for (i = 0; signature && i < 4; i++) {
		if (mbr->partition[i].os_type == EFI_PMBR_OSTYPE_EFI_GPT) {
			found = 1;
			break;
		}
	}
	return (signature && found);
}

/**
 * compare_gpts() - Search disk for valid GPT headers and PTEs
 * @pgpt is the primary GPT header
 * @agpt is the alternate GPT header
 * @lastlba is the last LBA number
 * Description: Returns nothing.  Sanity checks pgpt and agpt fields
 * and prints warnings on discrepancies.
 * 
 */
static void
compare_gpts(gpt_header *pgpt, gpt_header *agpt, uint64_t lastlba)
{
	int error_found = 0;
	if (!pgpt || !agpt)
		return;
	if (pgpt->my_lba != agpt->alternate_lba) {
		fprintf(stderr, "GPT:Primary header LBA != Alt. header alternate_lba\n");
		fprintf(stderr, "GPT:%" PRIx64 " != %" PRIx64 "\n",
		       pgpt->my_lba, agpt->alternate_lba);
		error_found++;
	}
	if (pgpt->alternate_lba != agpt->my_lba) {
		fprintf(stderr, "GPT:Primary header alternate_lba != Alt. header my_lba\n");
		fprintf(stderr, "GPT:%" PRIx64 " != %" PRIx64 "\n",
		       pgpt->alternate_lba, agpt->my_lba);
		error_found++;
	}
	if (pgpt->first_usable_lba != agpt->first_usable_lba) {
		fprintf(stderr, "GPT:first_usable_lbas don't match.\n");
		fprintf(stderr, "GPT:%" PRIx64 " != %" PRIx64 "\n",
		       pgpt->first_usable_lba, agpt->first_usable_lba);
		error_found++;
	}
	if (pgpt->last_usable_lba != agpt->last_usable_lba) {
		fprintf(stderr, "GPT:last_usable_lbas don't match.\n");
		fprintf(stderr, "GPT:%" PRIx64 " != %" PRIx64 "\n",
		       pgpt->last_usable_lba, agpt->last_usable_lba);
		error_found++;
	}
	if (efi_guidcmp(pgpt->disk_guid, agpt->disk_guid)) {
		fprintf(stderr, "GPT:disk_guids don't match.\n");
		error_found++;
	}
	if (pgpt->num_partition_entries != agpt->num_partition_entries) {
		fprintf(stderr, "GPT:num_partition_entries don't match: "
		       "0x%x != 0x%x\n",
		       pgpt->num_partition_entries,
		       agpt->num_partition_entries);
		error_found++;
	}
	if (pgpt->sizeof_partition_entry != agpt->sizeof_partition_entry) {
		fprintf(stderr, 
		       "GPT:sizeof_partition_entry values don't match: "
		       "0x%x != 0x%x\n", pgpt->sizeof_partition_entry,
		       agpt->sizeof_partition_entry);
		error_found++;
	}
	if (pgpt->partition_entry_array_crc32 !=
	    agpt->partition_entry_array_crc32) {
		fprintf(stderr, "GPT:partition_entry_array_crc32 values don't match: "
		       "0x%x != 0x%x\n", pgpt->partition_entry_array_crc32,
		       agpt->partition_entry_array_crc32);
		error_found++;
	}
	if (pgpt->alternate_lba != lastlba) {
		fprintf(stderr, "GPT:Primary header thinks Alt. header is not at the end of the disk.\n");
		fprintf(stderr, "GPT:%" PRIx64 " != %" PRIx64 "\n",
		       pgpt->alternate_lba, lastlba);
		error_found++;
	}

	if (agpt->my_lba != lastlba) {
		fprintf(stderr, "GPT:Alternate GPT header not at the end of the disk.\n");
		fprintf(stderr, "GPT:%" PRIx64 " != %" PRIx64 "\n",
		       agpt->my_lba, lastlba);
		error_found++;
	}

	if (error_found)
		fprintf(stderr, 
		       "GPT: Use GNU Parted to correct GPT errors.\n");
	return;
}


/************************************************************
 * FindValidGPT()
 * Requires:
 *  - fd
 *  - pgpt is a GPTH if it's valid
 *  - agpt is a GPTH if it's valid
 *  - ptes is a PTE
 * Modifies:
 *  - gpt & ptes
 * Returns:
 *   1 if valid
 *   0 on error
 ************************************************************/
static int
FindValidGPT(int fd, gpt_header ** pgpt, gpt_header ** agpt, gpt_entry ** ptes)
{
	int good_pgpt=0, good_agpt=0, good_pmbr=0;
	gpt_entry *pptes = NULL, *aptes = NULL;
        legacy_mbr *legacymbr=NULL;
	uint64_t lastlba;

	lastlba = LastLBA(fd);
	/* Check the Primary GPT */
	good_pgpt = IsGuidPartitionTableValid(fd, 1, pgpt, &pptes);
	if (good_pgpt) {
		/* Primary GPT is OK, check the alternate and warn if bad */
		good_agpt = IsGuidPartitionTableValid(fd, (*pgpt)->alternate_lba,
                                                      agpt, &aptes);
		if (!good_agpt) {
			*agpt = NULL;
			fprintf(stderr, "Alternate GPT is invalid, using primary GPT.\n");
		}

                compare_gpts(*pgpt, *agpt, lastlba);
                
		if (aptes)
			free(aptes);
		*ptes = pptes;
	} /* if primary is valid */
	else {
		/* Primary GPT is bad, check the Alternate GPT */
		*pgpt = NULL;
		good_agpt = IsGuidPartitionTableValid(fd, lastlba, agpt, &aptes);
		if (good_agpt) {
			/* Primary is bad, alternate is good.
			   Return values from the alternate and warn.
			 */
			fprintf(stderr, "Primary GPT is invalid, using alternate GPT.\n");
			*ptes = aptes;
		}
	}

	/* Now test for valid PMBR */
	/* This will be added to the EFI Spec. per Intel after v1.02. */
	if (good_pgpt || good_agpt) {
		legacymbr = malloc(sizeof (*legacymbr));
		if (legacymbr) {
			memset(legacymbr, 0, sizeof (*legacymbr));
			ReadLBA(fd, 0, (uint8_t *) legacymbr, sizeof (*legacymbr));
			good_pmbr = is_pmbr_valid(legacymbr);
			free(legacymbr);
		}
		if (good_pmbr)
			return 1;
		if (opts.forcegpt) {
			fprintf(stderr, "Warning: Disk has a valid GPT signature but invalid PMBR.\n");
			fprintf(stderr, "gpt option taken, disk treated as GPT.\n");
			fprintf(stderr, "Use GNU Parted to correct disk.\n");
			return 1;
                } else {
			fprintf(stderr, "Warning: Disk has a valid GPT signature but invalid PMBR.\n");
			fprintf(stderr, "Assuming this disk is *not* a GPT disk anymore.\n");
			fprintf(stderr, "Use -g or --gpt option to override.  Use GNU Parted to correct disk.\n");
                }
        }

	/* Both primary and alternate GPTs are bad.
	 * This isn't our disk, return 0.
	 */
	*pgpt = NULL;
	*agpt = NULL;
	*ptes = NULL;
	return 0;
}

/************************************************************
 * gpt_disk_get_partition_info()
 * Requires:
 *  - open file descriptor fd
 *  - start, size, signature, mbr_type, signature_type
 * Modifies: all these
 * Returns:
 *  0 on success
 *  non-zero on failure
 *
 ************************************************************/
int
gpt_disk_get_partition_info(int fd,
			    uint32_t num,
			    uint64_t * start, uint64_t * size,
			    char *signature,
			    uint8_t * mbr_type, uint8_t * signature_type)
{
	gpt_header *pgpt = NULL, *agpt = NULL, *gpt = NULL;
	gpt_entry *ptes = NULL, *p;

	if (!FindValidGPT(fd, &pgpt, &agpt, &ptes))
		return -1;

	if (pgpt)
		gpt = pgpt;
	else if (agpt)
		gpt = agpt;

	*mbr_type = 0x02;
	*signature_type = 0x02;

	if (num > 0) {
		p = &ptes[num - 1];
		*start = p->starting_lba;
		*size = p->ending_lba - p->starting_lba + 1;
		memcpy(signature, &p->unique_partition_guid,
		       sizeof (p->unique_partition_guid));
	} else {
		*start = 0;
		*size = LastLBA(fd) + 1;
		memcpy(signature, &gpt->disk_guid, sizeof (gpt->disk_guid));
	}
	return 0;
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 4 
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -4
 * c-argdecl-indent: 4
 * c-label-offset: -4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */
