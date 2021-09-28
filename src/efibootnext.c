/*
 * efibootnext - Attempt to set a BootNext variable from existing boot
 * options.
 *
 * Copyright 2015 Red Hat, Inc.
 * Author: Peter Jones <pjones@redhat.com>
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

#include "fix_coverity.h"

#include <alloca.h>
#include <err.h>
#include <errno.h>
#include <string.h>
#include <popt.h>

typedef enum {
 notnot, // just a regular match sense
 ignore, // ignore this match - only consider its children
 not,    // ! <match>
 and,    // <match> -a <match>
 or,     // <match> -o <match>
} match_sense;

struct matcher {
	int sense;
	struct matcher *matchers;

	char *bootnum;
	char *disk;
	char *edd;
	char *edd_devnum;
	char *loader;
	char *label;
	int gpt;
	int in_boot_order;
};

int
main(int argc, char *argv[])
{
	int err_if_not_found = 1;
	int err_if_set_fails = 1;

	int which = 0;
	char *sorter = NULL;

	struct matcher *matchers;
	struct matcher *matcher;

	poptContext optCon;

	matcher = matchers = calloc(1, sizeof(struct matcher));
	struct poptOption matchopts[] = {
		/* options to specify match criteria */
		{"bootnum", 'n', POPT_ARG_STRING, matcher->bootnum, NULL,
			"boot entry number (hex)", "<####>" },
		{"disk", 'd', POPT_ARG_STRING, matcher->disk, NULL,
			"disk containing loader", "<disk>" },
		/* keep edd and device together despite alphabetism */
		{"edd", 'e', POPT_ARG_STRING, matcher->edd, NULL, 
			"EDD version", "[1|3|[any]]" },
		{"device", 'E', POPT_ARG_STRING, matcher->edd_devnum, NULL,
			"EDD 1.0 device number (hex)", "[##|[80]]", },
		/* keep gpt and mbr together despite alphabetism */
		{"gpt", 'g', POPT_ARG_VAL, &matcher->gpt, 1,
			"only match GPT partitioned disks", },
		{"mbr", 'm', POPT_ARG_VAL, &matcher->gpt, 2,
			"only match MBR partitioned disks", },
		{"loader", 'l', POPT_ARG_STRING, matcher->loader, NULL,
			"loader path", "<path>", },
		{"label", 'L', POPT_ARG_STRING, matcher->label, NULL,
			"boot entry label", "<label>", },
		/* keep i-b-o and n-i-b-o together. */
		{"in-boot-order", 'b', POPT_ARG_VAL,
			&matcher->in_boot_order, 1,
			"only match entries in the boot order", },
		{"not-in-boot-order", 'B', POPT_ARG_VAL,
			&matcher->in_boot_order, 2,
			"only match entires not in the boot order", },

		POPT_TABLEEND
	};

	struct poptOption options[] = {
		{NULL, '\0', POPT_ARG_INTL_DOMAIN, "efibootnext" },
		/* options not about our match criteria */
		{"missing-ok", 'm', POPT_ARG_VAL, &err_if_not_found, 0,
			"return success if there's no variable matching "
			"the criteria", },
		{"ignore-efi-errors", 'i', POPT_ARG_VAL, &err_if_set_fails, 0,
			"return success if setting UEFI variables fails", },

		/* options about determining /which/ match to use */
		{"use-first", 'f', POPT_ARG_VAL, &which, 0,
			"use the first matching entry", },
		{"use-last", 'F', POPT_ARG_VAL, &which, 1,
			"use the last matching entry", },
		{"sorter", 's', POPT_ARG_STRING, &sorter, NULL,
			"run <sorter_path> to sort matches", "<sorter_path>", },

		/* options to specify match criteria */
		/* these are also in matchopts, but with different vals */
		{"bootnum", 'n', POPT_ARG_NONE|POPT_ARGFLAG_DOC_HIDDEN, NULL,
			'n', NULL, NULL },
		{"disk", 'd', POPT_ARG_NONE|POPT_ARGFLAG_DOC_HIDDEN, NULL,
			'd', NULL, NULL },
		/* keep edd and device together despite alphabetism */
		{"edd", 'e', POPT_ARG_NONE|POPT_ARGFLAG_DOC_HIDDEN, NULL,
			'e', NULL, NULL },
		{"device", 'E', POPT_ARG_NONE|POPT_ARGFLAG_DOC_HIDDEN, NULL,
			'E', NULL, NULL },
		/* keep gpt and mbr together despite alphabetism */
		{"gpt", 'g', POPT_ARG_NONE|POPT_ARGFLAG_DOC_HIDDEN, NULL,
			'g', NULL, NULL },
		{"mbr", 'm', POPT_ARG_NONE|POPT_ARGFLAG_DOC_HIDDEN, NULL,
			'm', NULL, NULL },
		{"loader", 'l', POPT_ARG_NONE|POPT_ARGFLAG_DOC_HIDDEN, NULL,
			'l', NULL, NULL },
		{"label", 'L', POPT_ARG_NONE|POPT_ARGFLAG_DOC_HIDDEN, NULL,
			'L', NULL, NULL },
		/* keep i-b-o and n-i-b-o together. */
		{"in-boot-order", 'b', POPT_ARG_NONE|POPT_ARGFLAG_DOC_HIDDEN,
			NULL, 'b', NULL, NULL },
		{"not-in-boot-order", 'B',
			POPT_ARG_NONE|POPT_ARGFLAG_DOC_HIDDEN, NULL,
			'B', NULL, NULL },
		/* and the things that can start the first match group */
		{"not", '!', POPT_ARG_NONE|POPT_ARGFLAG_DOC_HIDDEN, NULL, '!',
			NULL, NULL },
		{"start", '(', POPT_ARG_NONE|POPT_ARGFLAG_DOC_HIDDEN, NULL, '(',
			NULL, NULL },
		POPT_AUTOALIAS
		POPT_AUTOHELP
		POPT_TABLEEND
	};
	int rc;

	optCon = poptGetContext("efibootnext", argc, (const char **)argv,
				options, 0);
	poptSetOtherOptionHelp(optCon, "[OPTIONS]* <match criteria>");

	if (!matcher)
		err(3, "Could not allocate memory");
	memset(matchers, 0, sizeof (*matchers));

	rc = poptReadDefaultConfig(optCon, 0);
	if (rc < 0 && !(rc == POPT_ERROR_ERRNO && errno == ENOENT))
		errx(1, "poptReadDefaultConfig failed: %s", poptStrerror(rc));

	while ((rc = poptGetNextOpt(optCon)) > 0) {
		switch (rc) {
		case 'n':
			break;
		case 'd':
			break;
		case 'e':
			break;
		case 'E':
			break;
		case 'g':
			break;
		case 'm':
			break;
		case 'l':
			break;
		case 'L':
			break;
		case 'b':
			break;
		case 'B':
			break;
		case '!':
			break;
		case '(':
			break;
	}

	if (rc < -1)
		errx(2, "Invalid argument: \"%s\": %s",
		     poptBadOption(optCon, 0), poptStrerror(rc));

	if (poptPeekArg(optCon))
		errx(2, "Invalid argument: \"%s\"", poptPeekArg(optCon));

	poptFreeContext(optCon);

	return 0;
}

