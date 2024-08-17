#include <stdio.h>
#include <limits.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#include "rlimit.h"

#define __NEW_STARLET 1
#include <descrip.h>
#include <efndef.h>
#include <jpidef.h>
#include <prcdef.h>
#include <prvdef.h>
#include <stsdef.h>
#include <syidef.h>
#include <iosbdef.h>
#include <iledef.h>
#include <starlet.h>


static int notimplemented()
{
    errno = ENOSYS;
    return (-1);
}

static int curpriv(PRVDEF * priv)
{
    int status;
    unsigned short retlen;
    ILE3 itemlist[2];
    IOSB jpi_iosb;

    /* Soft limit is from sys$getjpiw */
    itemlist[0].ile3$w_length = 4;
    itemlist[0].ile3$w_code = JPI$_CURPRIV;
    itemlist[0].ile3$ps_bufaddr = priv;
    itemlist[0].ile3$ps_retlen_addr = &retlen;
    itemlist[1].ile3$w_length = 0;
    itemlist[1].ile3$w_code = 0;

    status = sys$getjpiw(EFN$C_ENF,
			 0, NULL, itemlist, &jpi_iosb, NULL, 0);

    if (! $VMS_STATUS_SUCCESS(status)) {
	errno = EVMSERR;
	vaxc$errno = status;
	return (-1);
    }

    if (! $VMS_STATUS_SUCCESS(jpi_iosb.iosb$w_status)) {
	errno = EVMSERR;
	vaxc$errno = jpi_iosb.iosb$w_status;
	return (-1);
    }

    return (0);
}

static int getjpi(int jpi_code, struct rlimit *rlim)
{
    int status;
    int result = 0;
    unsigned short retlen;
    ILE3 itemlist[2];
    IOSB jpi_iosb;

    /* Soft limit is from sys$getjpiw */
    itemlist[0].ile3$w_length = 4;
    itemlist[0].ile3$w_code = jpi_code;
    itemlist[0].ile3$ps_bufaddr = &result;
    itemlist[0].ile3$ps_retlen_addr = &retlen;
    itemlist[1].ile3$w_length = 0;
    itemlist[1].ile3$w_code = 0;

    status = sys$getjpiw
	(EFN$C_ENF, 0, NULL, itemlist, &jpi_iosb, NULL, 0);

    if (! $VMS_STATUS_SUCCESS(status)) {
	errno = EVMSERR;
	vaxc$errno = status;
	return (-1);
    }

    if (! $VMS_STATUS_SUCCESS(jpi_iosb.iosb$w_status)) {
	errno = EVMSERR;
	vaxc$errno = jpi_iosb.iosb$w_status;
	return (-1);
    }

    /* Usually if the result is 0, we want to return -1 */
    if (result == 0) {
	rlim->rlim_cur = -1;
	rlim->rlim_max = -1;
    } else {
	rlim->rlim_cur = result;
	rlim->rlim_max = result;
    }
    return (0);
}

int getrlimit(int resource, struct rlimit *rlim)
{
    int rv;
#ifdef RLIMIT_SIGPENDING
    sigset_t signal_set;
#endif
    PRVDEF cur_priv;

    if (rlim == NULL) {
	errno = EINVAL;
	return (-1);
    }

    switch (resource) {
    case RLIMIT_CPU:
	/* CPU time limit in seconds */
	rv = getjpi(JPI$_CPULIM, rlim);
	if (rv < 0) {
	    return (rv);
	}

	/* Use 0 for unlimited CPU */
	if (rlim->rlim_cur == -1UL) {
	    rlim->rlim_cur = RLIM_INFINITY;
	    rlim->rlim_max = rlim->rlim_cur;
	}
	return (rv);

    case RLIMIT_FSIZE:
        /* This is probably a bit simplistic */
	rlim->rlim_cur = LONG_MAX;
	rlim->rlim_max = LONG_MAX;
	return (0);

    case RLIMIT_NPROC:
	/* Number of subprocesses allowed */
	return (getjpi(JPI$_PRCLM, rlim));

    case RLIMIT_NOFILE:
	/* Max number of open files */
	return (getjpi(JPI$_FILLM, rlim));

    case RLIMIT_RSS:
	/* Working set extent - physical memory */
	rv = getjpi(JPI$_WSEXTENT, rlim);
	if (rv < 0) {
	    return (rv);
	}

	/* Need to adjust based on VAX page sizes */
	rlim->rlim_cur = rlim->rlim_cur / 2;
	rlim->rlim_max = rlim->rlim_cur;
	return (rv);

    case RLIMIT_AS:
	/* Virtual address space in use */
	rv = getjpi(JPI$_VIRTPEAK, rlim);
	if (rv < 0) {
	    return (rv);
	}

	/* Need to adjust based on VAX page sizes */
	rlim->rlim_cur = rlim->rlim_cur / 2;
	rlim->rlim_max = rlim->rlim_cur;
	return (rv);

    case RLIMIT_LOCKS:
	/* Locks allowed to be held */
	return (getjpi(JPI$_ENQLM, rlim));

    case RLIMIT_NICE:
	rv = curpriv(&cur_priv);
	if (rv < 0) {
	    return (rv);
	}
	rv = getjpi(JPI$_AUTHPRI, rlim);
	if (rv < 0) {
	    return (rv);
	}

	if (rlim->rlim_cur == -1UL) {
	    rlim->rlim_cur = 0;
	    rlim->rlim_max = rlim->rlim_cur;
	}

	/* UNIX priority == -1 * (VMS priority - 4) */
	rlim->rlim_cur = (rlim->rlim_cur - 4) * -1;

	/* If JPI$_CURPRIV has ALTPRI return VMS 0-15, else JPI$_AUTHPRI is the max */
	if (cur_priv.prv$v_setpri != 0) {
	    rlim->rlim_max = rlim->rlim_cur;
	} else {
	    rlim->rlim_max = 15 - 4;	/* UNIX inverts the sign for max */

	}
	return (rv);

    case RLIMIT_CORE:
	/* Can only get the flag at process creation time */
	/* Otherwise needs kernel mode code */

	rv = getjpi(JPI$_CREPRC_FLAGS, rlim);
	if (rv < 0) {
	    return (rv);
	}
	if ((rlim->rlim_cur & PRC$M_IMGDMP) != 0) {
	    rlim->rlim_cur = RLIM_INFINITY;
	} else {
	    rlim->rlim_cur = 0;
	}
	rlim->rlim_max = RLIM_INFINITY;
	return (rv);

#ifdef RLIMIT_SIGPENDING
    case RLIMIT_SIGPENDING:
	rv = sigfillset(&signal_set);
	if (rv < 0) {
	    return (rv);
	}
	rlim->rlim_cur = 0;
	rv = sigpending(&signal_set);
	if (rv < 0) {
	    return (rv);
	} else {
	    int i;
	    unsigned int sig1, sig2;
	    sig1 = signal_set._set[0];
	    sig2 = signal_set._set[1];
	    i = 0;
	    while ((sig1 != 0) && (i < 32)) {
		if (sig1 & 1) {
		    rlim->rlim_cur++;
		}
		sig1 = sig1 >> 1;
	    }
	    i = 0;
	    while ((sig2 != 0) && (i < 32)) {
		if (sig2 & 1) {
		    rlim->rlim_cur++;
		}
		sig2 = sig2 >> 1;
	    }
	}
	rlim->rlim_max = rlim->rlim_cur;
	return (rv);
#endif

#ifdef RLIMIT_RTPRIO
    case RLIMIT_RTPRIO:
#endif
#ifdef RLIMIT_MEMLOCK
    case RLIMIT_MEMLOCK:
#endif
#ifdef RLIMIT_PTHREAD
    case RLIMIT_PTHREAD:
#endif
#ifdef RLIMIT_STACK
    case RLIMIT_STACK:
#endif
#ifdef RLIMIT_DATA
    case RLIMIT_DATA:
#endif
#ifdef RLIMIT_SMGQUEUE
    case RLIMIT_MSGQUEUE:
#endif
#ifdef RLMIT_SBSIZE
    case RLIMIT_SBSIZE:
#endif
	return (notimplemented());
    }
    errno = EINVAL;
    return (-1);
}

int setrlimit(int resource, const struct rlimit *rlim)
{

    if (rlim == NULL) {
	errno = EINVAL;
	return (-1);
    }

    switch (resource) {
    case RLIMIT_CPU:
    case RLIMIT_FSIZE:
    case RLIMIT_CORE:
    case RLIMIT_RSS:
    case RLIMIT_NPROC:
    case RLIMIT_NOFILE:
    case RLIMIT_AS:
    case RLIMIT_LOCKS:
    case RLIMIT_SIGPENDING:
    case RLIMIT_NICE:
#ifdef RLIMIT_RTPRIO
    case RLIMIT_RTPRIO:
#endif
#ifdef RLIMIT_MEMLOCK
    case RLIMIT_MEMLOCK:
#endif
#ifdef RLIMIT_PTHREAD
    case RLIMIT_PTHREAD:
#endif
#ifdef RLIMIT_STACK
    case RLIMIT_STACK:
#endif
#ifdef RLIMIT_DATA
    case RLIMIT_DATA:
#endif
#ifdef RLIMIT_SMGQUEUE
    case RLIMIT_MSGQUEUE:
#endif
#ifdef RLMIT_SBSIZE
    case RLIMIT_SBSIZE:
#endif
	return (notimplemented());
    }
    errno = EINVAL;
    return (-1);
}
