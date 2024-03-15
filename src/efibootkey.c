/*
 * efibootkey.c - create hotkeys for boot options.
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


#include <stdio.h>
#include <stdlib.h>
#include <efivar.h>
#include <popt.h>
#include "efi.h"
#include "error.h"
#include "efibootkey.h"
#include "crc32.h"

int verbose = 0;

int
calc_crc_boot(uint16_t boot_num, uint32_t *crc)
{
    char boot_name[9];
    uint32_t attributes;
    uint8_t *data = NULL;
    size_t data_size = 0;
    int rc;
    sprintf(boot_name, "Boot%04d", boot_num);
    rc = efi_get_variable(EFI_GLOBAL_GUID, boot_name, &data,
                          &data_size, &attributes);
    if (rc < 0) {
        return rc;
    }
    *crc = efi_crc32(data, data_size);
    return 0;
}

static void
show_keys(int check_crc)
{
    char **names = NULL;
    uint8_t *data = NULL;
    size_t data_size = 0;
    efi_key_option_t *key_option;
    uint32_t attributes, crc;
    int i, j, rc;
    rc = read_var_names("Key", &names);
    if (rc < 0 || !names) {
        return;
    }
    for (i=0; names[i] != NULL; i++) {
        rc = efi_get_variable(EFI_GLOBAL_GUID, names[i], &data,
                              &data_size, &attributes);
        if (rc < 0) {
            warning("Skipping unreadable variable \"%s\"",
					names[i]);
            goto skipvar;
        }
        key_option = (efi_key_option_t *) data;
        printf("%s: for=Boot%04X ", names[i], key_option->boot_option);
        printf(" key=");
        if (key_option->key_data.options.shift_pressed) {
            printf("Shift+");
        }
        if (key_option->key_data.options.control_pressed) {
            printf("Ctrl+");
        }
        if (key_option->key_data.options.alt_pressed) {
            printf("Alt+");
        }
        if (key_option->key_data.options.logo_pressed) {
            printf("Logo+");
        }
        if (key_option->key_data.options.menu_pressed) {
            printf("Menu+");
        }
        if (key_option->key_data.options.sysrq_pressed) {
            printf("SysRq+");
        }
        for (j = 0; j < key_option->key_data.options.input_key_count; j++) {
            if (j > 0) {
                printf("+");
            }
            if (key_option->keys[j].scan_code) {
                printf("%s", get_scan_code(key_option->keys[j].scan_code));
            } else {
                char *s = ucs2_to_utf8_string(key_option->keys[j].unicode_char);
                if (!s) {
                    error(8, "Could not allocate memory");
                }
                printf("%s", s);
                free(s);
            }
        }
        if (check_crc) {
            rc = calc_crc_boot(key_option->boot_option, &crc);
            if (rc < 0) {
                printf(" crc=unreadable");
            } else if (crc == key_option->boot_option_crc) {
                printf(" crc=ok");
            } else {
                printf(" crc=invalid");
            }
        }
        printf("\n");
        skipvar:
        free(data);
    }
    free(names);
}

int
verify_support()
{
    uint8_t *data = NULL;
    size_t data_size = 0;
    uint32_t attributes;
    int rc;
    rc = efi_get_variable(EFI_GLOBAL_GUID, "BootOptionSupport", &data,
                          &data_size, &attributes);
    if (rc < 0) {
        warning("Skipping unreadable variable \"BootOptionSupport\"");
        return -1;
    }
    if (data_size > 0 && data[0] & 0x01) {
        printf("Supported\n");
        if (data_size > 1) {
            printf("Maximum number keys: %d\n", data[1] & 0x3);
        }
        return 0;
    }
    return  -1;
}

int
get_free_num()
{
    char **names = NULL;
    int i, num, rc;
    rc = read_var_names("Key", &names);
    if (rc < 0 || !names) {
        return 0;
    }
    for (i=0; names[i] != NULL; i++) {
        sscanf(names[i], "Key%x", &num);
        if (num != i) {
            break;
        }
    }
    free(names);
    return i;
}

int
create_key(int bootnum, char **keycode_v, efi_boot_key_data_t keydata)
{
    size_t size = sizeof(efi_key_option_t)
                + sizeof(efi_input_key_t) * keydata.options.input_key_count;
    uint32_t crc;
    efi_key_option_t *key = malloc(size);
    if (!key) {
        error(8, "Could not allocate memory");
    }
    key->boot_option = bootnum;
    key->key_data = keydata;
    if (calc_crc_boot(bootnum, &crc) < 0) {
        error(10, "Could not gerate CRC");
    }
    key->boot_option_crc = crc;
    for (int i = 0; i < keydata.options.input_key_count; i++) {
        if (utf8len(keycode_v[i]) == 1) {
            char32_t c = utf8charat(keycode_v[i], 0);
            key->keys[i].unicode_char = utf8_to_ucs2(c);
        } else {
            key->keys[i].scan_code = scan_code_from_name(keycode_v[i]);
            if (key->keys[i].scan_code == SCAN_NULL) {
                errorx(10, "Invalid key name: %s\n", keycode_v[i]);
            }
        }
    }
    int num = get_free_num();
    char name[8];
    int rc;
    sprintf(name, "Key%04X", num);
    uint32_t attributes = EFI_VARIABLE_NON_VOLATILE |
					      EFI_VARIABLE_BOOTSERVICE_ACCESS |
					      EFI_VARIABLE_RUNTIME_ACCESS;
    rc = efi_set_variable(EFI_GLOBAL_GUID, name, (uint8_t *) key, size,
                        attributes, 0644);
    if (rc < 0) {
        error(10, "Error writing variable");
    }
    free(key);
    return 0;
}

int
delete_key(int keynum)
{
    char name[8];
    int r;
    sprintf(name, "Key%04X", keynum);
    r = efi_del_variable(EFI_GLOBAL_GUID, name);
    if (r < 0) {
        error(10, "Error deleting variable");
    }
    return 0;
}

int
main(int argc, char **argv)
{
    int bootnum = -1;
    int keynum = -1;
    char *hexnum;
    char *keycode;
    char *keycode_v[3];
    int keycode_c = 0;
    int alt = 0, shift = 0, ctrl = 0, logo = 0, menu = 0, sysrq = 0;
    poptContext poptcon;
    int verify = 0, list = 0, create = 0, delete = 0;
    int check_crc = 0;
    char rc;

    struct poptOption options[] = {
        {.argInfo = POPT_ARG_INTL_DOMAIN,
         .arg = "efibootkey"},
        {.argInfo = POPT_ARG_STRING,
         .longName = "bootnum",
         .shortName = 'b',
         .arg = &hexnum,
         .val = 'b',
         .descrip = "Hot key for BootXXXX"},
        {.argInfo = POPT_ARG_STRING,
         .longName = "keynum",
         .shortName = 'n',
         .arg = &hexnum,
         .val = 'n',
         .descrip = "Modify KeyXXXX"},
        {.argInfo = POPT_ARG_STRING,
         .longName = "key",
         .shortName = 'k',
         .arg = &keycode,
         .val = 'k',
         .descrip = "Code of Key, can repeat up to three times"},
        {.argInfo = POPT_ARG_NONE,
         .longName = "alt",
         .arg = &alt,
         .descrip = "Alt must be pressed"},
        {.argInfo = POPT_ARG_NONE,
         .longName = "shift",
         .arg = &shift,
         .descrip = "Shift must be pressed"},
        {.argInfo = POPT_ARG_NONE,
         .longName = "ctrl",
         .arg = &ctrl,
         .descrip = "Control must be pressed"},
        {.argInfo = POPT_ARG_NONE,
         .longName = "logo",
         .arg = &logo,
         .descrip = "Logo must be pressed"},
        {.argInfo = POPT_ARG_NONE,
         .longName = "menu",
         .arg = &menu,
         .descrip = "Menu must be pressed"},
        {.argInfo = POPT_ARG_NONE,
         .longName = "sysrq",
         .arg = &sysrq,
         .descrip = "SysReq must be pressed"},
        {.argInfo = POPT_ARG_NONE,
         .longName = "support",
         .arg = &verify,
         .descrip = "Verify if boot manager support hot key"},
        {.argInfo = POPT_ARG_NONE,
         .longName = "list",
         .shortName = 'l',
         .arg = &list,
         .descrip = "List hot keys"},
        {.argInfo = POPT_ARG_NONE,
         .longName = "checkcrc",
         .arg = &check_crc,
         .descrip = "Check CRC"},
        {.argInfo = POPT_ARG_NONE,
         .longName = "delete-keynum",
         .shortName = 'B',
         .arg = &delete,
         .descrip = "Delete hot key"},
		POPT_AUTOHELP
		POPT_TABLEEND
    };

    poptcon = poptGetContext(NULL, argc, (const char **)argv, options, 0);
    if (argc < 2) {
        poptPrintUsage(poptcon, stderr, 0);
        exit(1);
    }

    while ((rc = poptGetNextOpt(poptcon)) >=0) {
        switch (rc) {
        case 'k':
            if (keycode_c >= 3) {
                poptPrintUsage(poptcon, stderr, 0);
                exit(4);
            }
            create = 1;
            keycode_v[keycode_c++] = keycode;
            break;
        case 'b':
        case 'n':
            if (!hexnum) {
                errorx(4, "Invalid hexdecimal number\n");
            }
            char *endptr;
            unsigned long num;
            num = strtoul(hexnum, &endptr, 16);
            if ((num == ULONG_MAX && errno == ERANGE)
                    || *endptr != '\0') {
                error(4, "Invalid hexdecimal number");
            }
            if (rc == 'b')
                bootnum = num;
            else
                keynum = num;
            break;
        }
    }

	if (rc < -1)
		errorx(2, "Invalid argument: \"%s\": %s",
		       poptBadOption(poptcon, 0), poptStrerror(rc));

    if (verify) {
        return verify_support();
    } else if (list) {
        show_keys(check_crc);
        return 0;
    } else if (create) {
        if (bootnum < 0 || keycode_c == 0) {
            poptPrintUsage(poptcon, stderr, 0);
            exit(4);
        }
        efi_boot_key_data_t keydata = {
            .options = {
                .shift_pressed = shift,
                .control_pressed = ctrl,
                .alt_pressed = alt,
                .logo_pressed = logo,
                .menu_pressed = menu,
                .sysrq_pressed = sysrq,
                .input_key_count = keycode_c,
            }
        };
        create_key(bootnum, keycode_v, keydata);
    } else if (delete) {
        delete_key(keynum);
    }
    return 0;
}
