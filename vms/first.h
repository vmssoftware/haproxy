#define TPROXY
#define ENABLE_POLL

#define CONFIG_HAPROXY_VERSION "1.7.9"
#define CONFIG_HAPROXY_DATE "2022/03/24"

#define BUILD_TARGET "VSI OpenVMS"
#define BUILD_ARCH ""
#define BUILD_CPU "ia64"
#define BUILD_CC "CC"
#define BUILD_CFLAGS ""
#define BUILD_OPTIONS ""

#ifndef __x86_64
#define socklen_t unsigned
#endif

// Need Lua 5.3 or higher; current port is 5.2 (20-Nov-2017)
// #define USE_LUA

#define USE_OPENSSL
#define USE_PTHREAD_PSHARED

#define __FUNCTION__ __func__
