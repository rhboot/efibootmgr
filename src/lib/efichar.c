/*
  efichar.[ch]

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

  Best as I can tell, efi_char16_t characters are unicode characters,
  but are encoded in little-endian UCS-2
        http://www.cl.cam.ac.uk/~mgk25/unicode.html.
  libunicode expects characters to be encoded in UTF-8, so I can't use that.
  Therefore, we need a UCS-2 library.
*/

#include <stdlib.h>
#include <string.h>
#include "efi.h"
#include "efichar.h"

int
efichar_char_strcmp(const char *s1, const efi_char16_t *s2)
{
	int i, rc;
	char *buffer;
	int s2_len = efichar_strlen(s2, -1);

	buffer = malloc(s2_len+1);
	if (!buffer) return -1;
	memset(buffer, 0, s2_len+1);

	for (i=0; i<(s2_len); i++) {
		buffer[i] = s2[i] & 0xFF;
	}
	buffer[i] = '\0';
	rc = strcmp(s1, buffer);
	free(buffer);
	return rc;
}

int
efichar_strcmp(const efi_char16_t *s1, const efi_char16_t *s2)
{
	int i;
	int s1_len = efichar_strlen(s1, -1);
	int s2_len = efichar_strlen(s2, -1);


	for (i=0; i < s1_len && i < s2_len; i++) {
		if (s1[i] < s2[i]) return -1;
		if (s1[i] > s2[i]) return 1;

	}
	/* Hit the end of one string */
	if (i == s1_len && i != s2_len) return -1;
	if (i != s1_len && i == s2_len) return  1;
	return 0;
}

unsigned long
efichar_from_char(efi_char16_t *dest, const char *src, size_t dest_len)
{
	int i, src_len = strlen(src);
	for (i=0; i < src_len && i < (dest_len/sizeof(*dest)) - 1; i++) {
		dest[i] = src[i];
	}
	dest[i] = 0;
	return i * sizeof(*dest);
}

unsigned long
efichar_to_char(char *dest, const efi_char16_t *src, size_t dest_len)
{
	int i, src_len = efichar_strlen(src, -1);
	for (i=0; i < src_len && i < (dest_len/sizeof(*dest)) - 1; i++) {
		dest[i] = src[i];
	}
	dest[i] = 0;
	return i;
}

int
efichar_strlen(const efi_char16_t *p, int max)
{
	int len=0;
	const efi_char16_t *start = p;

	if (!p || !*p)   return 0;

	while ((max < 0 || p - start < max) && *(p+len))
	{
		++len;
	}
	return len;
}

#ifndef MIN
#define MIN(a, b)  (((a) < (b)) ? (a) : (b))
#endif

int
efichar_strncpy(efi_char16_t *dest, const efi_char16_t *src, int max)
{
	int i;
	int src_len = efichar_strlen(src, max);

	for (i=0; i < MIN(max,src_len); i++)
	{
		dest[i] = src[i];
	}
	dest[i] = 0;
	return i;
}

int
efichar_strsize(const efi_char16_t *p)
{
	return (efichar_strlen(p, -1) + 1) * sizeof(efi_char16_t);
}
