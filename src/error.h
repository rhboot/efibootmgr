/*
  error.h - some error functions to work with efivars error logger.

  Copyright 2016 Red Hat, Inc.  <pjones@redhat.com>

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
#ifndef EFIBOOTMGR_ERROR_H__
#define EFIBOOTMGR_ERROR_H__ 1

extern int verbose;

static inline void
__attribute__((__unused__))
error_reporter(void)
{
	int rc = 1;
	int saved_errno = errno;

        for (int i = 0; rc > 0; i++) {
                char *filename = NULL;
                char *function = NULL;
                int line = 0;
                char *message = NULL;
                int error = 0;

                rc = efi_error_get(i, &filename, &function, &line, &message,
                                   &error);
                if (rc < 0) {
			fprintf(stderr, "error fetching trace value");
			exit(1);
		}
                if (rc == 0)
                        break;
                fprintf(stderr, " %s:%d %s(): %s: %s\n",
			filename, line, function, message, strerror(error));
        }
	errno = saved_errno;
}

static inline void
__attribute__((__unused__))
conditional_error_reporter(int show, int clear)
{
	int saved_errno = errno;
	fflush(NULL);

	if (show) {
		fprintf(stderr, "error trace:\n");
		error_reporter();
	}
	if (clear) {
		errno = 0;
		efi_error_clear();
	}
	errno = saved_errno;
}

static inline void
__attribute__((__unused__))
cond_error(int test, int eval, const char *fmt, ...)
{
	int saved_errno = errno;
	if (!test)
		return;
	fflush(NULL);

	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	errno = saved_errno;
	fprintf(stderr, ": %m\n");
	conditional_error_reporter(verbose >= 1, 0);
	va_end(ap);
	exit(eval);
}

static inline void
__attribute__((__unused__))
error(int eval, const char *fmt, ...)
{
	int saved_errno = errno;
	fflush(NULL);

	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	errno = saved_errno;
	fprintf(stderr, ": %m\n");
	conditional_error_reporter(verbose >= 1, 0);
	va_end(ap);
	exit(eval);
}

static inline void
__attribute__((__unused__))
errorx(int eval, const char *fmt, ...)
{
	fflush(NULL);

	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	conditional_error_reporter(verbose >= 1, 1);
	va_end(ap);
	exit(eval);
}

static inline void
__attribute__((__unused__))
cond_warning(int test, const char *fmt, ...)
{
	int saved_errno = errno;
	if (!test)
		return;

	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	errno = saved_errno;
	printf(": %m\n");
	conditional_error_reporter(verbose >= 1, 1);
	va_end(ap);
}

static inline void
__attribute__((__unused__))
warning(const char *fmt, ...)
{
	int saved_errno = errno;
	va_list ap;

	va_start(ap, fmt);
	vprintf(fmt, ap);
	errno = saved_errno;
	printf(": %m\n");
	conditional_error_reporter(verbose >= 1, 1);
	va_end(ap);
}

static inline void
__attribute__((__unused__))
warningx(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprintf(fmt, ap);
	printf("\n");
	conditional_error_reporter(verbose >= 1, 1);
	va_end(ap);
}
#endif /* EFIBOOTMGR_ERROR_H__ */
