/*
  unparse_path.[ch]

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

/* For PRIx64 */
#define __STDC_FORMAT_MACROS

#include "fix_coverity.h"

#include <fcntl.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "efi.h"
#include "parse_loader_data.h"

/* Avoid unaligned access warnings */
#define get(buf, obj) *(typeof(obj) *)memcpy(buf, &obj, sizeof(obj))

extern int verbose;

ssize_t
parse_efi_guid(char *buffer, size_t buffer_size, uint8_t *p, uint64_t length)
{
	ssize_t needed = 0;

	if (length == sizeof(efi_guid_t)) {
		needed = efi_guid_to_id_guid((efi_guid_t *)p, NULL);
		if (buffer && needed > 0 && buffer_size >= (size_t)needed)
			needed = efi_guid_to_id_guid((efi_guid_t *)p, &buffer);
	}

	return needed;
}

ssize_t
parse_raw_text(char *buf, size_t buf_size, uint8_t *p, uint64_t length)
{
	uint64_t i;
	unsigned char c;
	bool print_hex = false;

	ssize_t needed;
	size_t buf_offset = 0;

	for (i=0; i < length; i++) {
		c = p[i];
		if (c < 32 || c > 127)
			print_hex = true;
	}
	for (i=0; i < length; i++) {
		c = p[i];
		needed = snprintf(buf + buf_offset,
				  buf_size == 0 ? 0 : buf_size - buf_offset,
				  print_hex ? "%02hhx" : "%c", c);
		if (needed < 0)
			return -1;
		buf_offset += needed;
	}
	return buf_offset;
}
