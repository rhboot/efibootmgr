// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * crc32.h - headers for crc32
 *
 */

#ifndef _CRC32_H
#define _CRC32_H

#include <stdint.h>

/*
 * This computes a 32 bit CRC of the data in the buffer, and returns the CRC.
 * The polynomial used is 0xedb88320.
 */

extern uint32_t crc32 (const void *buf, unsigned long len, uint32_t seed);

/**
 * efi_crc32() - EFI version of crc32 function
 * @buf: buffer to calculate crc32 of
 * @len - length of buf
 *
 * Description: Returns EFI-style CRC32 value for @buf
 *
 * This function uses the little endian Ethernet polynomial
 * but seeds the function with ~0, and xor's with ~0 at the end.
 * Note, the EFI Specification, v1.02, has a reference to
 * Dr. Dobbs Journal, May 1994 (actually it's in May 1992).
 */
static inline uint32_t
efi_crc32(const void *buf, unsigned long len)
{
	return (crc32(buf, len, ~0L) ^ ~0L);
}


#endif /* _CRC32_H */

// vim:fenc=utf-8:tw=75:noet
