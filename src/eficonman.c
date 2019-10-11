/*
 * eficonman.c - console manager for UEFI
 *
 * Copyright 2015 Red Hat, Inc.
 *
 * See "COPYING" for license terms.
 *
 * Author: Peter Jones <pjones@redhat.com>
 */

#include "fix_coverity.h"

#include <efivar.h>
#include <err.h>
#include <inttypes.h>
#include <libintl.h>
#include <locale.h>
#include <popt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define  _(String) gettext (String)
#define Q_(String) dgettext (NULL, String)
#define C_(Context,String) dgettext (Context,String)

/* ConIn-8be4df61-93ca-11d2-aa0d-00e098032b8c
 * ConInDev-8be4df61-93ca-11d2-aa0d-00e098032b8c
 * ConOut-8be4df61-93ca-11d2-aa0d-00e098032b8c
 * ConOutDev-8be4df61-93ca-11d2-aa0d-00e098032b8c
 * ErrOut-8be4df61-93ca-11d2-aa0d-00e098032b8c
 * ErrOutDev-8be4df61-93ca-11d2-aa0d-00e098032b8c
 * Key0000-8be4df61-93ca-11d2-aa0d-00e098032b8c
 * Key0001-8be4df61-93ca-11d2-aa0d-00e098032b8c
 * Lang-8be4df61-93ca-11d2-aa0d-00e098032b8c
 * LangCodes-8be4df61-93ca-11d2-aa0d-00e098032b8c
 */

#define ACTION_INACTION		0x00
#define ACTION_INFO		0x01

static int
do_list(void)
{
	struct {
		char *varname;
		char *label;
	} vars[] = {
		{"ConInDev", "Available console input devices"},
		{"ConOutDev", "Available console ouput devices"},
		{"ErrOutDev", "Available error output devices"},
		{"ConIn", "Configured console input devices"},
		{"ConOut", "Configured console ouptut devices"},
		{"ErrOut", "Configured error output devices"},
		{NULL, NULL}
	};
	for (int i = 0; vars[i].varname != NULL; i++) {
		uint8_t *data;
		size_t data_size;
		uint32_t attrs;
		int rc;
		const_efidp whole_dp, dp;

		rc = efi_get_variable(efi_guid_global, vars[i].varname,
				      &data, &data_size, &attrs);
		if (rc < 0) {
			printf("%s: none\n", vars[i].label);
			continue;
		}
		whole_dp = (const_efidp)data;
		printf("%s:\n", vars[i].label);
		if (!efidp_is_valid(whole_dp, data_size)) {
			printf("\tdata is invalid\n");
			continue;
		}
		dp = whole_dp;
		while (dp) {
			ssize_t sz, ssz;
			unsigned char *s = NULL;

			if (efidp_is_multiinstance(dp)) {
				sz = efidp_instance_size(dp);
				if (sz < 0)
					err(1, "efidp_instance_size()");
			} else {
				sz = efidp_size(dp);
				if (sz < 0)
					err(1, "efidp_size()");
			}

			ssz = efidp_format_device_path(NULL, 0, dp, sz);
			if (ssz < 0)
				err(1, "efidp_format_device_path()");

			s = alloca(ssz + 1);
			ssz = efidp_format_device_path(s, ssz, dp, sz);
			if (ssz < 0)
				err(1, "efidp_format_device_path()");
			s[ssz] = '\0';
			printf("\t%s\n", s);

			if (!efidp_is_multiinstance(dp))
				break;

			rc = efidp_get_next_end(dp, &dp);
			if (rc < 0)
				break;

			rc = efidp_next_instance(dp, &dp);
			if (rc < 0)
				break;
		}
	}
	return 0;
}

int
main(int argc, char *argv[])
{
	int action = 0;
	int quiet = 0;

	setlocale(LC_ALL, "");
	bindtextdomain("eficonman", LOCALEDIR);
	textdomain("eficonman");

	struct poptOption options[] = {
		{.argInfo = POPT_ARG_INTL_DOMAIN,
		 .arg = "eficonman" },
		{.longName = "info",
		 .shortName = 'i',
		 .argInfo = POPT_ARG_VAL|POPT_ARGFLAG_OR,
		 .arg = &action,
		 .val = ACTION_INFO,
		 .descrip = _("Display console information"), },
		{.longName = "quiet",
		 .shortName = 'q',
		 .argInfo = POPT_ARG_VAL,
		 .arg = &quiet,
		 .val = 1,
		 .descrip = _("Work quietly"), },
		POPT_AUTOALIAS
		POPT_AUTOHELP
		POPT_TABLEEND
	};

	poptContext optcon;
	optcon = poptGetContext("eficonman", argc, (const char **)argv, options, 0);

	int rc;
	rc = poptReadDefaultConfig(optcon, 0);
	if (rc < 0 && !(rc == POPT_ERROR_ERRNO && errno == ENOENT))
		errx(1, _("poptReadDefaultConfig failed: %s: %s"),
			poptBadOption(optcon, 0), poptStrerror(rc));

	while ((rc = poptGetNextOpt(optcon)) > 0)
		;

	switch (action) {
	case ACTION_INFO:
		do_list();
		break;
	case ACTION_INACTION:
	default:
		poptPrintUsage(optcon, stderr, 0);
		exit(1);
	}
	return 0;
}
