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
*/

#ifndef _EFICHAR_H
#define _EFICHAR_H

int efichar_strlen(const efi_char16_t *p, int max);
int efichar_strsize(const efi_char16_t *p);
unsigned long efichar_from_char(efi_char16_t *dest, const char *src, size_t dest_len);
unsigned long efichar_to_char(char *dest, const efi_char16_t *src, size_t dest_len);
int efichar_strcmp(const efi_char16_t *s1, const efi_char16_t *s2);
int efichar_char_strcmp(const char *s1, const efi_char16_t *s2);
int efichar_strncpy(efi_char16_t *s1, const efi_char16_t *s2, int max);

#endif
