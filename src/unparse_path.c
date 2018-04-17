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

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <netinet/in.h>

#include "efi.h"
#include "unparse_path.h"

/* Avoid unaligned access warnings */
#define get(buf, obj) *(typeof(obj) *)memcpy(buf, &obj, sizeof(obj))

ssize_t
unparse_raw_text(char *buffer, size_t buffer_size, uint8_t *p, uint64_t length)
{
	uint64_t i; unsigned char c;

	ssize_t needed;
	size_t buf_offset = 0;

	for (i=0; i < length; i++) {
		c = p[i];
		if (c < 32 || c > 127) c = '.';
		needed = snprintf(buffer + buf_offset,
			buffer_size == 0 ? 0 : buffer_size - buf_offset,
			"%c", c);
		if (needed < 0)
			return -1;
		buf_offset += needed;
	}
	return buf_offset;
}
