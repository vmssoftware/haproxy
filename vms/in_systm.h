#ifndef _OPENVMS_IN_SYSTM_H
#define _OPENVMS_IN_SYSTM_H

#ifndef __VMS
#include <sys/cdefs.h>
#endif
#include <sys/types.h>


/*
 * Network order versions of various data types. Unfortunately, BSD
 * assumes specific sizes for shorts (16 bit) and longs (32 bit) which
 * don't hold in general. As a consequence, the network order versions
 * may not reflect the actual size of the native data types.
 */

typedef u_int16_t n_short;      /* short as received from the net */
typedef u_int32_t n_long;       /* long as received from the net  */
typedef u_int32_t n_time;       /* ms since 00:00 GMT, byte rev   */


#endif
