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


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "crc32.h"
#include "gpt.h"


#define BLKGETLASTSECT  _IO(0x12,108) /* get last sector of block device */
#define BLKGETSIZE      _IO(0x12,96)  /* return device size */

struct blkdev_ioctl_param {
        unsigned int block;
        size_t content_length;
        char * block_contents;
};



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
IsLegacyMBRValid(LegacyMBR_t * mbr)
{
	return (mbr ? (mbr->Signature == MSDOS_MBR_SIGNATURE) : 0);
}

static inline int
efi_guidcmp(efi_guid_t left, efi_guid_t right)
{
	return memcmp(&left, &right, sizeof(efi_guid_t));
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
 *  - if the disk has an odd number of sectors, return
 *    one minus the actual value, as Linux does reads in
 *    blocks of 2. :(
 ************************************************************/

static uint64_t
LastLBA(int filedes)
{
  int rc;
  uint64_t sectors = 0;
  struct stat s;
  memset(&s, 0, sizeof(s));
  rc = fstat(filedes, &s);
  if (rc == -1) {
    fprintf(stderr, "LastLBA() could not stat: %s\n", strerror(errno));
    return 0;
  }

  if (S_ISBLK(s.st_mode)) {
    rc = ioctl(filedes, BLKGETSIZE, &sectors);
    if (rc) {
      fprintf(stderr, "LastLBA() ioctl error: %s\n", strerror(errno));
      return 0;
    }
  }

  else {
    fprintf(stderr, "LastLBA(): I don't know how to handle files with mode %x\n", s.st_mode);
    sectors = 0;
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
read_lastoddsector(int fd, uint64_t lba, void *buffer, size_t count)
{
        int rc;
        struct blkdev_ioctl_param ioctl_param;

        if (!buffer) return 0; 

        ioctl_param.block = 0; /* read the last sector */
        ioctl_param.content_length = count;
        ioctl_param.block_contents = buffer;

        rc = ioctl(fd, BLKGETLASTSECT, &ioctl_param);
        if (rc == -1) perror("read failed");

        return !rc;

}


static int
ReadLBA(int fd, uint64_t lba, void *buffer, size_t bytes)
{
        off_t offset = lba * 512;

        /* Kludge.  This is necessary to read/write the last
           block of an odd-sized disk, until Linux 2.5.x kernel fixes.
           This is only used by gpt.c, and only to read
           one sector, so we don't have to be fancy.
        */
        if (!(LastLBA(fd) & 1) && lba == LastLBA(fd)) {
                return read_lastoddsector(fd, lba, buffer, bytes);
        }
        
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
static GuidPartitionEntry_t *
ReadGuidPartitionEntries(int fd,
                         GuidPartitionTableHeader_t *
                         gpt)
{
	GuidPartitionEntry_t *pte;

	pte = (GuidPartitionEntry_t *)
                malloc(gpt->NumberOfPartitionEntries *
                       gpt->SizeOfPartitionEntry);

        if (!pte) return NULL;

        memset(pte, 0, gpt->NumberOfPartitionEntries *
               gpt->SizeOfPartitionEntry);


	if (!ReadLBA(fd, gpt->PartitionEntryLBA, pte,
		     gpt->NumberOfPartitionEntries *
		     gpt->SizeOfPartitionEntry)) {
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
static GuidPartitionTableHeader_t *
ReadGuidPartitionTableHeader(int fd,
                             uint64_t lba)
{
	GuidPartitionTableHeader_t *gpt;
	gpt = (GuidPartitionTableHeader_t *)
                malloc(sizeof(GuidPartitionTableHeader_t));
        if (!gpt) return NULL;
        memset(gpt, 0, sizeof (*gpt));
	if (!ReadLBA(fd, lba, gpt, sizeof(GuidPartitionTableHeader_t))) {
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
			  GuidPartitionTableHeader_t ** gpt,
			  GuidPartitionEntry_t ** ptes)
{
	int rc = 0;		/* default to not valid */
	uint32_t crc, origcrc;
        
        if (!gpt || !ptes) return rc;

        // printf("IsGuidPartitionTableValid(%llx)\n", lba);
	if (!(*gpt = ReadGuidPartitionTableHeader(fd, lba)))
		return rc;
	/* Check the GUID Partition Table Signature */
	if ((*gpt)->Signature != GPT_HEADER_SIGNATURE) {
                /* 
                printf("GUID Partition Table Header Signature is wrong: %llx != %llx\n",
			(*gpt)->Signature, GUID_PT_HEADER_SIGNATURE);
                */
		free(*gpt);
		*gpt = NULL;
		return rc;
	}

	/* Check the GUID Partition Table Header CRC */
	origcrc = (*gpt)->HeaderCRC32;
	(*gpt)->HeaderCRC32 = 0;
	crc = efi_crc32(*gpt, (*gpt)->HeaderSize);
	if (crc != origcrc) {
		// printf( "GPTH CRC check failed, %x != %x.\n", origcrc, crc);
		(*gpt)->HeaderCRC32 = origcrc;
		free(*gpt);
		*gpt = NULL;
		return rc;
	}
	(*gpt)->HeaderCRC32 = origcrc;
	/* Check that the MyLBA entry points to the LBA
	   that contains the GPT we read */
	if ((*gpt)->MyLBA != lba) {
		// printf( "MyLBA %llx != lba %llx.\n", (*gpt)->MyLBA, lba);
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
	crc = efi_crc32(*ptes, (*gpt)->NumberOfPartitionEntries *
			   (*gpt)->SizeOfPartitionEntry);
	if (crc != (*gpt)->PartitionEntryArrayCRC32) {
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
FindValidGPT(int fd,
	     GuidPartitionTableHeader_t ** pgpt,
             GuidPartitionTableHeader_t ** agpt,
	     GuidPartitionEntry_t       ** ptes)
{
	int rc = 0;
	GuidPartitionEntry_t *pptes = NULL, *aptes = NULL;
	uint64_t lastlba;


	lastlba = LastLBA(fd);
	/* Check the Primary GPT */
	rc = IsGuidPartitionTableValid(fd, 1, pgpt, &pptes);
	if (rc) {
		/* Primary GPT is OK, check the alternate and warn if bad */
		rc = IsGuidPartitionTableValid(fd, (*pgpt)->AlternateLBA,
					       agpt, &aptes);

		if (!rc) {
                        *agpt = NULL;
                        printf("Alternate GPT is invalid, using primary GPT.\n");

		}
		if (aptes) free(aptes);
		*ptes = pptes;
		return 1;
	} /* if primary is valid */
	else {
		/* Primary GPT is bad, check the Alternate GPT */
                *pgpt = NULL;
		rc = IsGuidPartitionTableValid(fd, lastlba,
					       agpt, &aptes);
		if (rc) {
			/* Primary is bad, alternate is good.
			   Return values from the alternate and warn.
			 */
			printf
			    ("Primary GPT is invalid, using alternate GPT.\n");
			*ptes = aptes;
			return 1;
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
gpt_disk_get_partition_info (int fd, 
                             uint32_t num,
                             uint64_t *start, uint64_t *size,
                             char *signature,
                             uint8_t *mbr_type, uint8_t *signature_type)
{	
        GuidPartitionTableHeader_t * pgpt=NULL, * agpt=NULL, *gpt = NULL;
        GuidPartitionEntry_t       * ptes=NULL, *p;

        
	if (!FindValidGPT(fd, &pgpt, &agpt, &ptes)) return -1;
        
        if (pgpt)      gpt = pgpt;
        else if (agpt) gpt = agpt;

        *mbr_type = 0x02;
        *signature_type = 0x02;

        if (num > 0) {
                p = &ptes[num-1];
                *start = p->StartingLBA;
                *size  = p->EndingLBA - p->StartingLBA + 1;
                memcpy(signature, &p->UniquePartitionGuid,
                       sizeof(p->UniquePartitionGuid));
        }
        else {
                *start = 0;
                *size = LastLBA(fd) + 1;
                memcpy(signature, &gpt->DiskGUID, sizeof(gpt->DiskGUID));
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
