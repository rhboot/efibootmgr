/*
 * efibootkey.h - create hotkeys for boot options.
 *
 * Copyright 2023 Demitrius Belai
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this library; if not, see <https://www.gnu.org/licenses/>.
 *
 */

#ifndef _EFIBOOTKEY_H
#define _EFIBOOTKEY_H

#include <string.h>
#include <stdint.h>
#include <uchar.h>
#include <wchar.h>

typedef struct {
    uint16_t scan_code;
    char16_t unicode_char;
} __attribute__ ((packed)) efi_input_key_t;

typedef union {
    struct {
        uint32_t revision: 8;
        uint32_t shift_pressed: 1;
        uint32_t control_pressed: 1;
        uint32_t alt_pressed: 1;
        uint32_t logo_pressed: 1;
        uint32_t menu_pressed: 1;
        uint32_t sysrq_pressed: 1;
        uint32_t reserved: 16;
        uint32_t input_key_count: 2;
    } options;
    uint32_t packed_value;
} __attribute__ ((packed)) efi_boot_key_data_t;

typedef struct {
    efi_boot_key_data_t key_data;
    uint32_t boot_option_crc;
    uint16_t boot_option;
    efi_input_key_t keys[];
} __attribute__ ((packed)) efi_key_option_t;

/*
 * utf8_to_ucs2(): extracted from efivar/ucs2.h
 */
static inline char16_t
utf8_to_ucs2(const char32_t c)
{
    const char *utf8 = (char *) &c;
    if ((utf8[0] & 0xe0) == 0xe0 && !(utf8[0] & 0x10)) {
        return ((utf8[0] & 0x0f) << 12)
                |((utf8[1] & 0x3f) << 6)
                |((utf8[2] & 0x3f) << 0);
    } else if ((utf8[0] & 0xc0) == 0xc0 && !(utf8[0] & 0x20)) {
        return ((utf8[0] & 0x1f) << 6)
                |((utf8[1] & 0x3f) << 0);
    } else {
        return utf8[0] & 0x7f;
    }
}

#define ev_bits(val, mask, shift) \
	(((val) & ((mask) << (shift))) >> (shift))

/*
 * ucs2_to_utf8(): extracted from efivar/ucs2.h
 */
static inline
char32_t ucs2_to_utf8(const char16_t c)
{
    char ret[sizeof(char32_t)];
    memset(&ret, 0, sizeof(char32_t));
    if (c <= 0x7f) {
        ret[0] = c;
    } else if (c > 0x7f && c <= 0x7ff) {
        ret[0] = 0xc0 | ev_bits(c, 0x1f, 6);
        ret[1] = 0x80 | ev_bits(c, 0x3f, 0);
    } else if (c > 0x7ff) {
        ret[0] = 0xe0 | ev_bits(c, 0xf, 12);
        ret[1] = 0x80 | ev_bits(c, 0x3f, 6);
        ret[2] = 0x80| ev_bits(c, 0x3f, 0);
    }
    return *((char32_t*) &ret);
}

static inline
char * ucs2_to_utf8_string(const char16_t c)
{
    void *s = malloc(sizeof(char32_t) + 1);
    if (!s) {
        return NULL;
    }
    ((char *)s)[sizeof(char32_t)] = '\0';
    char32_t val = ucs2_to_utf8(c);
    *((char32_t *)s) = val;
    return s;
}

static inline
size_t utf8len(const char *s)
{
    size_t len = 0;
    for (; *s; s++)
        if ((*s & 0xc0) != 0x80)
            len++;
    return len;
}

static inline
char32_t utf8charat(const char *s, size_t at)
{
    size_t len = 0;
    char32_t c = 0;
    char *b = (char *)&c;
    for (; *s; s++) {
        if ((*s & 0xc0) != 0x80) {
            if (++len > at)
                break;
        }
    }
    *b = *s;
    if ((*s & 0xc0) == 0xc0)
        for (s++, b++; (*s & 0xc0) == 0x80; s++, b++)
            *b = *s;
    return c;
}

// EFI Scan codes
#define SCAN_NULL               0x0000
#define SCAN_UP                 0x0001
#define SCAN_DOWN               0x0002
#define SCAN_RIGHT              0x0003
#define SCAN_LEFT               0x0004
#define SCAN_HOME               0x0005
#define SCAN_END                0x0006
#define SCAN_INSERT             0x0007
#define SCAN_DELETE             0x0008
#define SCAN_PAGE_UP            0x0009
#define SCAN_PAGE_DOWN          0x000A
#define SCAN_F1                 0x000B
#define SCAN_F2                 0x000C
#define SCAN_F3                 0x000D
#define SCAN_F4                 0x000E
#define SCAN_F5                 0x000F
#define SCAN_F6                 0x0010
#define SCAN_F7                 0x0011
#define SCAN_F8                 0x0012
#define SCAN_F9                 0x0013
#define SCAN_F10                0x0014
#define SCAN_F11                0x0015
#define SCAN_F12                0x0016
#define SCAN_ESC                0x0017
#define SCAN_PAUSE              0x0048
#define SCAN_F13                0x0068
#define SCAN_F14                0x0069
#define SCAN_F15                0x006A
#define SCAN_F16                0x006B
#define SCAN_F17                0x006C
#define SCAN_F18                0x006D
#define SCAN_F19                0x006E
#define SCAN_F20                0x006F
#define SCAN_F21                0x0070
#define SCAN_F22                0x0071
#define SCAN_F23                0x0072
#define SCAN_F24                0x0073
#define SCAN_MUTE               0x007F
#define SCAN_VOLUME_UP          0x0080
#define SCAN_VOLUME_DOWN        0x0081
#define SCAN_BRIGHTNESS_UP      0x0100
#define SCAN_BRIGHTNESS_DOWN    0x0101
#define SCAN_SUSPEND            0x0102
#define SCAN_HIBERNATE          0x0103
#define SCAN_TOGGLE_DISPLAY     0x0104
#define SCAN_RECOVERY           0x0105
#define SCAN_EJECT              0x0106

typedef struct {
    uint16_t scan_code;
    char *name;
} scan_code_t;

//const int scan_codes_size = 47;

const scan_code_t scan_codes[] = {
    {SCAN_UP,                   "Up"},
    {SCAN_DOWN,                 "Down"},
    {SCAN_RIGHT,                "Right"},
    {SCAN_LEFT,                 "Left"},
    {SCAN_HOME,                 "Home"},
    {SCAN_END,                  "End"},
    {SCAN_INSERT,               "Insert"},
    {SCAN_DELETE,               "Delete"},
    {SCAN_PAGE_UP,              "PgUP"},
    {SCAN_PAGE_DOWN,            "PgDown"},
    {SCAN_F1,                   "F1"},
    {SCAN_F2,                   "F2"},
    {SCAN_F3,                   "F3"},
    {SCAN_F4,                   "F4"},
    {SCAN_F5,                   "F5"},
    {SCAN_F6,                   "F6"},
    {SCAN_F7,                   "F7"},
    {SCAN_F8,                   "F8"},
    {SCAN_F9,                   "F9"},
    {SCAN_F10,                  "F10"},
    {SCAN_F11,                  "F11"},
    {SCAN_F12,                  "F12"},
    {SCAN_ESC,                  "Esc"},
    {SCAN_PAUSE,                "Pause"},
    {SCAN_F13,                  "F13"},
    {SCAN_F14,                  "F14"},
    {SCAN_F15,                  "F15"},
    {SCAN_F16,                  "F16"},
    {SCAN_F17,                  "F17"},
    {SCAN_F18,                  "F18"},
    {SCAN_F19,                  "F19"},
    {SCAN_F20,                  "F20"},
    {SCAN_F21,                  "F21"},
    {SCAN_F22,                  "F22"},
    {SCAN_F23,                  "F23"},
    {SCAN_F24,                  "F24"},
    {SCAN_MUTE,                 "Mute"},
    {SCAN_VOLUME_UP,            "VolUp"},
    {SCAN_VOLUME_DOWN,          "VolDown"},
    {SCAN_BRIGHTNESS_UP,        "BrtUp"},
    {SCAN_BRIGHTNESS_DOWN,      "BrtDown"},
    {SCAN_SUSPEND,              "Suspend"},
    {SCAN_HIBERNATE,            "Hibernate"},
    {SCAN_TOGGLE_DISPLAY,       "Display"},
    {SCAN_RECOVERY,             "Recovery"},
    {SCAN_EJECT,                "Eject"},
    {SCAN_NULL,                 "NULL"},
};

const char *
get_scan_code(uint16_t scan_code)
{
    int i;
    for (i = 0; scan_codes[i].scan_code != SCAN_NULL; i++) {
        if (scan_code == scan_codes[i].scan_code)
            return scan_codes[i].name;
    }
    return "Unknown";
}

uint16_t
scan_code_from_name(const char *s)
{
    int i;
    for (i = 0; scan_codes[i].scan_code != SCAN_NULL; i++) {
        if (strcasecmp(scan_codes[i].name, s) == 0)
            return scan_codes[i].scan_code;
    }
    return SCAN_NULL;
}

#endif
