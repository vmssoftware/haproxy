/*
 * © Copyright 1976, 2003 Hewlett-Packard Development Company, L.P.
 *
 * Confidential computer software.  Valid license  from  HP  and/or its
 * subsidiaries required for possession, use, or copying.
 *
 * Consistent with FAR 12.211 and 12.212, Commercial Computer Software,
 * Computer Software Documentation,  and  Technical Data for Commercial
 * Items  are  licensed to the U.S. Government under vendor's  standard
 * commercial license.
 *
 * Neither HP nor any of its subsidiaries shall be liable for technical
 * or editorial errors or omissions contained herein.   The information
 * in  this document is provided  "as is"  without warranty of any kind
 * and is subject  to  change  without  notice.   The warranties for HP
 * products are set forth in the express  limited  warranty  statements
 * accompanying such products.   Nothing herein should be construed  as
 * constituting an additional warranty.
 */

#ifndef _IP6_H_
#define	_IP6_H_

#include <netinet/in6_machtypes.h>

#define IPV6VERSION	0x60
#define	IPVERSIONMASK	0xF0

#define IPV6_MINIMUM_LINK_MTU	1280		/* minimum link mtu */
#define IPV6_MAX_PAYLOAD	65535		/* max non-jumbo payload */
#define IPV6_MIN_JUMBO_PAYLOAD	65536		/* min jumbo payload */
#define IPV6_MAX_JUMBO_PAYLOAD	0xffffffff	/* max jumbo payload */
#define IPV6_REASS_TIME		60		/* reassembly timeout seconds */

#define	IPV6_ISALIGNED(d)	((((int)(d)) & (sizeof(u_long) - 1)) == 0)
/*
 * Define the base IPv6 header
 */

struct ip6_hdrctl;
struct ip6_hdr {
    union {
	struct ip6_hdrctl {
	    uint32_t ip6_un1_flow;	/* 4 bits version, 8 bits traffic
					   class, 20 bits flow label */
	    uint16_t ip6_un1_plen;	/* payload length */
	    uint8_t  ip6_un1_nxt;	/* next header */
	    uint8_t  ip6_un1_hlim;	/* hop limit */
	} ip6_un1;
	u_int8_t  ip6_un2_vfc;		/* 4 bits version, top 4 bits tclass */
	u_int32_t ip6_un3_ctldata[2];
    } ip6_ctlun;
    struct in6_addr ip6_src;		/* source address */
    struct in6_addr ip6_dst;		/* destination address */
};

#define ip6_vcf		ip6_ctlun.ip6_un1.ip6_un1_flow
#define ip6_vfc		ip6_ctlun.ip6_un2_vfc
#define	ip6_ctl		ip6_ctlun.ip6_un3_ctldata
#define ip6_flow	ip6_ctlun.ip6_un1.ip6_un1_flow
#define	ip6_plen	ip6_ctlun.ip6_un1.ip6_un1_plen
#define	ip6_nxt		ip6_ctlun.ip6_un1.ip6_un1_nxt
#define	ip6_hlim	ip6_ctlun.ip6_un1.ip6_un1_hlim
#define	ip6_hops	ip6_ctlun.ip6_un1.ip6_un1_hlim

#define IP6_VCF_VERSION_IPV6	    IN6__MK0_MSB32_VALUE(IPV6VERSION)
#define IP6_VCF_VERSION_MASK	    IN6__MK0_MSB32_VALUE(IPVERSIONMASK)
#define	IP6_VCF_TRAFFIC_CLASS_MASK  IN6__MK3_MSB32_VALUE(0x0F, 0xF0, 0, 0)
#define IP6_VCF_FLOW_LABEL_MASK	    IN6__MK3_MSB32_VALUE(0, 0x0F, 0xFF, 0xFF)
#define IP6_VCF_CLASS_FLOW_MASK	    (IP6_VCF_TRAFFIC_CLASS_MASK|IP6_VCF_FLOW_LABEL_MASK)


/****
 ****  IPV6 OPTIONS
 ****/

/*
 * Define the high two bits of the option type. This signifies the
 * actions to take when the implementation doesn't recognize the option
 */
#define IPV6_OPT_MASK		0xC0
#define IP6OPT_TYPE(o)		((o) & IPV6_OPT_MASK)
#define IP6OPT_TYPE_SKIP	0x00	/* Skip the option */
#define IP6OPT_TYPE_DISCARD	0x40	/* Discard the packet */
#define IP6OPT_TYPE_FORCEICMP	0x80	/* Discard, send an ICMP error msg */
#define IP6OPT_TYPE_ICMP	0xc0	/* Discard, send an ICMP msg only
					   if the dst addr is not multicast */
#define IP6OPT_MUTABLE		0x20	/* Option can change in flight */

#define IPV6_OPT_IGNORE		IP6OPT_TYPE_SKIP
#define IPV6_OPT_DISCARD	IP6OPT_TYPE_DISCARD
#define IPV6_OPT_ICMP		IP6OPT_TYPE_FORCEICMP
#define IPV6_OPT_ICMP_NOMCAST	IP6OPT_TYPE_ICMP

/*
 * Known option types
 */
#define IP6OPT_PAD1		0x00	/* 00 0 00000 */
#define IP6OPT_PADN		0x01	/* 00 0 00001 */
#define IP6OPT_JUMBO		0xc2	/* 11 0 00010 = 194 */
#define IP6OPT_NSAP_ADDR	0xc3	/* 11 0 00011 */
#define IP6OPT_TUNNEL_LIMIT	0x04	/* 00 0 00100 */
#define IP6OPT_ROUTER_ALERT	0x05	/* 00 0 00101 */
#define IP6OPT_HOME_ADDRESS	0xc9	/* 11 0 01001 */
#define IP6OPT_EID		0x8a	/* 10 0 01010 */

#define IPV6_OPT_PAD1		IP6OPT_PAD1
#define IPV6_OPT_PADN		IP6OPT_PADN
#define IPV6_OPT_JUMBO_PAYLOAD	IP6OPT_JUMBO


/*
 * Generic option header
 */
struct ip6_opt {
    u_int8_t  ip6o_type;
    u_int8_t  ip6o_len;
};

/* the minimum length of an option header */
#define IP6OPT_MINLEN	2


/*
 * Pad1 option
 */
struct ip6_opt_pad1 {
    u_int8_t  ip6op1_type;	/* option type (0) */
};
/* defines for 2292 compatability */
#define p1_type		ip6op1_type


/*
 * PadN option
 */
struct ip6_opt_padn {
    u_int8_t  ip6opn_type;	/* option type (1) */
    u_int8_t  ip6opn_dlen;	/* option data length (excludes type, dlen) */
				/* option data (variable length) */
};
/* defines for 2292 compatability */
#define pn_type		ip6opn_type
#define pn_dlen		ip6opn_dlen


/*
 * Jumbo Payload option
 */
struct ip6_opt_jumbo {
    u_int8_t  ip6oj_type;	/* option type (194) */
    u_int8_t  ip6oj_len;	/* option data length (4) */
    u_int8_t  ip6oj_jumbo_len[4];	/* jumbo payload length */
};
#define IP6OPT_JUMBO_LEN	6
/* defines for 2292 compatability */
#define ip6_opt_jumbo_payload	ip6_opt_jumbo
#define jp_type			ip6oj_type
#define jp_dlen			ip6oj_len
#define jp_plen			ip6oj_jumbo_len


/*
 * NSAP Address Option
*/
struct ip6_opt_nsap {
    uint8_t  ip6on_type;
    uint8_t  ip6on_len;
    uint8_t  ip6on_src_nsap_len;
    uint8_t  ip6on_dst_nsap_len;
    /* followed by source NSAP */
    /* followed by destination NSAP */
};


/*
 * Tunnel Limit Option
 */
struct ip6_opt_tunnel {
    uint8_t  ip6ot_type;
    uint8_t  ip6ot_len;
    uint8_t  ip6ot_encap_limit;
};


/*
 * Router Alert Option
 */
struct ip6_opt_router {
    uint8_t  ip6or_type;
    uint8_t  ip6or_len;
    uint8_t  ip6or_value[2];
};

/* Router alert values (in network byte order) */
#define	IP6_ALERT_MLD	0x0000
#define	IP6_ALERT_RSVP	0x0001
#define	IP6_ALERT_AN	0x0002


/*
 * Home Address Option
 */
struct ip6_opt_home_address {
    uint8_t  ip6oha_type;
    uint8_t  ip6oha_len;		/* length, always 16 */
    uint8_t  ip6oha_addr[16];		/* Home Address */
};

#define IP6OPT_HALEN		16	/* Length of HA option */


/***
 *** EXTENSION HEADERS
 ***/

/*
 * Hop-by-Hop options header
 */

struct ip6_hbh {
    uint8_t  ip6h_nxt;        /* next header */
    uint8_t  ip6h_len;        /* length in units of 8 octets */
           /* followed by options */
};


/*
 * Destination options header
 */

struct ip6_dest {
    uint8_t  ip6d_nxt;        /* next header */
    uint8_t  ip6d_len;        /* length in units of 8 octets */
    /* followed by options */
};


/*
 * Routing header
 */

struct ip6_rthdr {
    uint8_t  ip6r_nxt;        /* next header */
    uint8_t  ip6r_len;        /* length in units of 8 octets */
    uint8_t  ip6r_type;       /* routing type */
    uint8_t  ip6r_segleft;    /* segments left */
    /* followed by routing type specific data */
};


/*
 * Routing extension header, type 0
 */

struct ip6_rthdr0 {
    uint8_t  ip6r0_nxt;       /* next header */
    uint8_t  ip6r0_len;       /* length in units of 8 octets */
    uint8_t  ip6r0_type;      /* always zero */
    uint8_t  ip6r0_segleft;   /* segments left */
    uint32_t ip6r0_reserved;  /* reserved field */
    /* followed by up to 127 IPv6 addresses */
};


/*
 * Routing extension header, type 2
 */

struct ip6_rthdr2 {
    uint8_t  ip6r2_nxt;          /* next header */
    uint8_t  ip6r2_len;          /* length in units of 8 octets, always 2 */
    uint8_t  ip6r2_type;         /* always 2 */
    uint8_t  ip6r2_segleft;      /* always 1 */
    uint32_t ip6r2_reserved;     /* reserved field */
    struct in6_addr ip6r2_homeaddr;     /* Home Address */
};


/*
 * Fragment extension header
 */

struct ip6_frag {
    uint8_t   ip6f_nxt;       /* next header */
    uint8_t   ip6f_reserved;  /* reserved field */
    uint16_t  ip6f_offlg;     /* offset, reserved, and flag */
    uint32_t  ip6f_ident;     /* identification */
};

#define IP6F_OFF_MASK		0xfff8  /* mask out offset from _offlg */
#define IP6F_RESERVED_MASK	0x0006  /* reserved bits in ip6f_offlg */
#define IPV6_FR6_MORE		0x0001	/* more fragments */


/*
 * Authentication extension header, may be followed by variable length
 * authentication data which must be an integral number of 32-bit words.
 */
struct ip6_auhdr {
    u_int8_t  au6_nxt;       /* next header */
    u_int8_t  au6_len;       /* length of header minus 2 (in 32-bit units) */
    u_int16_t au6_res;       /* reserved... */
    u_int32_t au6_spi;       /* security parameter index */
    u_int32_t au6_seq;	     /* anti-replay sequence number */
};


/*
 * ESP headers
 */
struct ip6_esphdr {
    u_int32_t esp6_spi;      /* security parameter index */
    u_int32_t esp6_seq;      /* sequence number */
};

struct ip6_espfixedtrailer {
    u_int8_t  esp6t_pad_len; /* pad length */
    u_int8_t  esp6t_nxt;     /* next header */
};


#define	IN6_SET_IP6CTL_NORMAL(ip6, protocol, hops, payload_length) \
    do { \
	ip6->ip6_ctl[0] = IN6__MK0_MSB32_VALUE(IPV6VERSION); \
	ip6->ip6_ctl[1] = IN6__MK1_MSW32_VALUE(payload_length, IN6__MK1_MSB16_VALUE(protocol, hops)); \
    } while (0)

#endif /* _IP6_H_ */
