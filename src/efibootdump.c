/*
 * efibootdump.c - dump a variable as if it's a Boot#### variable
 *
 * Copyright 2015 Red Hat, Inc.
 *
 * See "COPYING" for license terms.
 *
 * Author: Peter Jones <pjones@redhat.com>
 */

#include "fix_coverity.h"

#include <ctype.h>
#include <efiboot.h>
#include <efivar.h>
#include <err.h>
#include <inttypes.h>
#include <libintl.h>
#include <locale.h>
#include <popt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <uchar.h>
#include <unistd.h>

#include "error.h"
#include "unparse_path.h"

int verbose;

#define  _(String) gettext (String)
#define Q_(String) dgettext (NULL, String)
#define C_(Context,String) dgettext (Context,String)

static void
print_boot_entry(efi_load_option *loadopt, size_t data_size)
{
	char *text_path = NULL;
	size_t text_path_len = 0;
	uint8_t *optional_data = NULL;
	size_t optional_data_len = 0;
	uint16_t pathlen;
	const unsigned char *desc;
	char *raw;
	size_t raw_len;

	ssize_t rc;
	efidp dp = NULL;

	printf("%c ", (efi_loadopt_attrs(loadopt) & LOAD_OPTION_ACTIVE)
	              ? '*' : ' ');

	desc = efi_loadopt_desc(loadopt, data_size);
	if (!desc)
		printf("<invalid description> ");
	else if (desc[0])
		printf("%s ", desc);

	dp = efi_loadopt_path(loadopt, data_size);
	pathlen = efi_loadopt_pathlen(loadopt, data_size);

	rc = efidp_format_device_path(NULL, 0, dp, pathlen);
	if (rc < 0) {
		printf("<bad device path>");
		return;
	}
	text_path_len = rc + 1;
	text_path = alloca(text_path_len);
	if (!text_path)
		error(100, "Couldn't allocate memory");
	rc = efidp_format_device_path(text_path, text_path_len,
				      dp, pathlen);
	if (rc < 0) {
		printf("<bad device path>");
		return;
	}
	if (text_path && text_path_len >= 1)
		printf("%s", text_path);

	rc = efi_loadopt_optional_data(loadopt, data_size,
				       &optional_data, &optional_data_len);
	if (rc < 0) {
		printf("<bad optional_data>");
		return;
	}

	rc = unparse_raw_text(NULL, 0, optional_data, optional_data_len);
	if (rc < 0) {
		printf("<bad optional data>");
		return;
	}

	raw_len = rc + 1;
	raw = alloca(raw_len);
	if (!raw)
		error(101, "Couldn't allocate memory");

	rc = unparse_raw_text(raw, raw_len, optional_data, optional_data_len);
	if (rc < 0) {
		printf("<bad optional data>");
	} else if (rc > 0) {
		for (unsigned int i = 0; i < optional_data_len; i++)
			putchar(isprint(optional_data[i])
				? optional_data[i]
				: '.');
	}

	printf("\n");
}

int
main(int argc, char *argv[])
{
	const char **names = NULL;
	const char **files = NULL;
	char *guidstr = NULL;
	efi_guid_t guid = efi_guid_global;

	setlocale(LC_ALL, "");
	bindtextdomain("efibootdump", LOCALEDIR);
	textdomain("efibootdump");

	struct poptOption options[] = {
		{.argInfo = POPT_ARG_INTL_DOMAIN,
		 .arg = "efibootdump" },
		{.longName = "guid",
		 .shortName = 'g',
		 .argInfo = POPT_ARG_STRING |
			    POPT_ARGFLAG_OPTIONAL |
			    POPT_ARGFLAG_STRIP,
		 .arg = &guidstr,
		 .descrip = _("GUID namespace the variable is in"),
		 .argDescrip = "{guid}"},
		{.longName = "file",
		 .shortName = 'f',
		 .argInfo = POPT_ARG_ARGV |
			    POPT_ARGFLAG_OPTIONAL |
			    POPT_ARGFLAG_STRIP,
		 .arg = &files,
		 .descrip = _("File to read variable data from"),
		 .argDescrip = "<file>"},
		{.longName = "verbose",
		 .shortName = 'v',
		 .argInfo = POPT_ARG_VAL |
			    POPT_ARGFLAG_OPTIONAL |
			    POPT_ARGFLAG_STRIP,
		 .arg = &verbose,
		 .val = 2,
		 .descrip = _("Be more verbose on errors"),
		},
		POPT_AUTOALIAS
		POPT_AUTOHELP
		POPT_TABLEEND
	};
	efi_load_option *loadopt;
	uint8_t *data = NULL;
	size_t data_size = 0;

	poptContext optcon;
	optcon = poptGetContext("efibootdump", argc, (const char **)argv,
				options, 0);

	poptSetOtherOptionHelp(optcon, "[OPTIONS...] [name0 [... [nameN]]]");

	int rc;
	rc = poptReadDefaultConfig(optcon, 0);
	if (rc < 0 && !(rc == POPT_ERROR_ERRNO && errno == ENOENT))
		errorx(1, _("poptReadDefaultConfig failed: %s: %s"),
		       poptBadOption(optcon, 0), poptStrerror(rc));

	while ((rc = poptGetNextOpt(optcon)) > 0)
		;

	if (rc < -1)
		errorx(2, "Invalid argument: \"%s\": %s",
		       poptBadOption(optcon, 0), poptStrerror(rc));

	/* argc = */ poptStrippedArgv(optcon, argc, argv);
	names = poptGetArgs(optcon);
	if (!names && !files) {
		poptPrintUsage(optcon, stderr, 0);
		exit(4);
	}

	if (names && (!names[0] || names[0][0] == '\0')) {
		poptPrintUsage(optcon, stderr, 0);
		exit(4);
	}

	if (files && (!files[0] || files[0][0] == '\0')) {
		poptPrintUsage(optcon, stderr, 0);
		exit(4);
	}

	if (names) {
		if (guidstr) {
			rc = efi_id_guid_to_guid(guidstr, &guid);
			if (rc < 0)
				error(5, "Could not parse guid \"%s\"",
				      guidstr);
		}
		free(guidstr);
		guidstr = NULL;
		rc = efi_guid_to_str(&guid, &guidstr);
		if (rc < 0)
			error(6, "Guid lookup failed");
	}

	for (unsigned int i = 0;
	     files != NULL && files[i] != NULL && files[i][0] != '\0';
	     i++) {
		struct stat statbuf;
		FILE *f;
		size_t n;
		const char *filename = files[i];

		memset(&statbuf, 0, sizeof(statbuf));
		rc = stat(filename, &statbuf);
		if (rc < 0)
			error(7, "Could not stat \"%s\"", filename);

		data_size = statbuf.st_size;
		if (data_size == 0)
			errorx(11, "File \"%s\" is empty", filename);

		data = alloca(data_size);
		if (data == NULL)
			error(8, "Could not allocate memory");

		f = fopen(filename, "r");
		if (!f)
			error(9, "Could not open \"%s\"", filename);

		n = fread(data, 1, data_size, f);
		if (n < data_size)
			error(10, "Could not read \"%s\"", filename);

		printf("%s: ", filename);
		loadopt = (efi_load_option *)(data + 4);
		if (data_size <= 8)
			errorx(11, "Data is not a valid load option");
		if (efi_loadopt_is_valid(loadopt, data_size - 4)) {
			print_boot_entry(loadopt, data_size - 4);
		} else {
			loadopt = (efi_load_option *)data;
			if (!efi_loadopt_is_valid(loadopt, data_size))
				errorx(11, "Data is not a valid load option");
			print_boot_entry(loadopt, data_size);
		}

		fclose(f);
	}

	for (unsigned int i = 0;
	     names && names[i] != NULL && names[i][0] != '\0';
	     i++) {
		uint32_t attrs = 0;

		rc = efi_get_variable(guid, names[i], &data, &data_size,
				      &attrs);
		if (rc < 0) {
			warning("couldn't read variable %s-%s",
				names[i], guidstr);
			continue;
		}

		loadopt = (efi_load_option *)data;
		if (!efi_loadopt_is_valid(loadopt, data_size)) {
			warning("load option for %s is not valid", names[i]);
			printf("%d\n", __LINE__);
			if (data && data_size > 0) {
				free(data);
				continue;
			}
		}

		printf("%s", names[i]);
		if (efi_guid_cmp(&efi_guid_global, &guid))
			printf("-%s", guidstr);
		printf(": ");
		print_boot_entry(loadopt, data_size);
		if (data && data_size > 0)
			free(data);
	}

	if (guidstr)
		free(guidstr);

	poptFreeContext(optcon);
	return 0;
}
