/*
  include/common/debug.h
  This files contains some macros to help debugging.

  Copyright (C) 2000-2006 Willy Tarreau - w@1wt.eu

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation, version 2.1
  exclusively.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef _COMMON_DEBUG_H
#define _COMMON_DEBUG_H

#include <common/config.h>
#include <common/memory.h>

#ifdef __VMS
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#endif

#ifdef DEBUG_FULL
#define DPRINTF(x...) fprintf(x)
#else
#ifdef __VMS
static inline void DPRINTF(FILE *fp, ...) {}
#else
#define DPRINTF(x...)
#endif
#endif

#ifdef DEBUG_FSM
#define FSM_PRINTF(x...) fprintf(x)
#else
#ifdef __VMS
static inline void FSM_PRINTF(FILE *fp, ...) {}
#else
#define FSM_PRINTF(x...)
#endif
#endif

/* This abort is more efficient than abort() because it does not mangle the
 * stack and stops at the exact location we need.
 */
#define ABORT_NOW() (*(int*)0=0)

/* this one is provided for easy code tracing.
 * Usage: TRACE(strm||0, fmt, args...);
 *        TRACE(strm, "");
 */
#ifdef __VMS
static inline void TRACE(void *strm, char *fmt, ...)
{
        char tmp[32];
        time_t t;
	va_list args;
        t = time(NULL);
        strftime(tmp, sizeof(tmp) - 1, "%d/%m/%y %H:%M:%S", localtime(&t));
	fprintf(stderr, "[%s] [%s:%d] ", tmp, __FILE__, __LINE__);
	va_start(args, fmt);
  	vfprintf (stderr, fmt, args);
	va_end(args);
  	fprintf(stderr, "\n");
}
#else
#define TRACE(strm, fmt, args...) do {                            \
	fprintf(stderr,                                           \
		"%d.%06d [%s:%d %s] [strm %p(%x)] " fmt "\n",      \
		(int)now.tv_sec, (int)now.tv_usec,                \
		__FILE__, __LINE__, __FUNCTION__,                 \
		strm, strm?((struct stream *)strm)->uniq_id:~0U, \
		##args);                                           \
        } while (0)
#endif

/* This one is useful to automatically apply poisonning on an area returned
 * by malloc(). Only "p_" is required to make it work, and to define a poison
 * byte using -dM.
 */
static inline void *p_malloc(size_t size)
{
	void *ret = malloc(size);
	if (mem_poison_byte >= 0 && ret)
		memset(ret, mem_poison_byte, size);
	return ret;
}

#endif /* _COMMON_DEBUG_H */
