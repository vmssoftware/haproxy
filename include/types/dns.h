/*
 * include/types/dns.h
 * This file provides structures and types for DNS.
 *
 * Copyright (C) 2014 Baptiste Assmann <bedis9@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation, version 2.1
 * exclusively.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef _TYPES_DNS_H
#define _TYPES_DNS_H

/*DNS maximum values */
/*
 * Maximum issued from RFC:
 *  RFC 1035: https://www.ietf.org/rfc/rfc1035.txt chapter 2.3.4
 *  RFC 2671: http://tools.ietf.org/html/rfc2671
 */
#define DNS_MAX_LABEL_SIZE	63
#define DNS_MAX_NAME_SIZE	255
#define DNS_MAX_UDP_MESSAGE	512

/* DNS minimun record size: 1 char + 1 NULL + type + class */
#define DNS_MIN_RECORD_SIZE	( 1 + 1 + 2 + 2 )

/* maximum number of query records in a DNS response
 * For now, we allow only one */
#define DNS_MAX_QUERY_RECORDS 1

/* maximum number of answer record in a DNS response */
#define DNS_MAX_ANSWER_RECORDS ((DNS_MAX_UDP_MESSAGE - DNS_HEADER_SIZE) / DNS_MIN_RECORD_SIZE)

/* size of dns_buffer used to store responses from the buffer
 * dns_buffer is used to store data collected from records found in a response.
 * Before using it, caller will always check that there is at least DNS_MAX_NAME_SIZE bytes
 * available */
#define DNS_ANALYZE_BUFFER_SIZE DNS_MAX_UDP_MESSAGE + DNS_MAX_NAME_SIZE

/* DNS error messages */
#define DNS_TOO_LONG_FQDN	"hostname too long"
#define DNS_LABEL_TOO_LONG	"one label too long"
#define DNS_INVALID_CHARACTER	"found an invalid character"

/* dns query class */
#define DNS_RCLASS_IN		1	/* internet class */

/* dns record types (non exhaustive list) */
#define DNS_RTYPE_A		1	/* IPv4 address */
#define DNS_RTYPE_CNAME		5	/* canonical name */
#define DNS_RTYPE_AAAA		28	/* IPv6 address */
#define DNS_RTYPE_ANY		255	/* all records */

/* dns rcode values */
#define DNS_RCODE_NO_ERROR	0	/* no error */
#define DNS_RCODE_NX_DOMAIN	3	/* non existent domain */
#define DNS_RCODE_REFUSED	5	/* query refused */

/* dns flags masks */
#define DNS_FLAG_TRUNCATED	0x0200	/* mask for truncated flag */
#define DNS_FLAG_REPLYCODE	0x000F	/* mask for reply code */

/* max number of network preference entries are avalaible from the
 * configuration file.
 */
#define SRV_MAX_PREF_NET 5

/* DNS header size */
#define DNS_HEADER_SIZE		sizeof(struct dns_header)

/* DNS request or response header structure */
struct dns_header {
	uint16_t id;
	uint16_t flags;
	uint16_t qdcount;
	uint16_t ancount;
	uint16_t nscount;
	uint16_t arcount;
#ifdef __VMS
};
#else
} __attribute__ ((packed));
#endif

/* short structure to describe a DNS question */
/* NOTE: big endian structure */
struct dns_question {
	unsigned short	qtype;		/* question type */
	unsigned short	qclass;		/* query class */
};

/* NOTE: big endian structure */
struct dns_query_item {
	struct list list;
	char name[DNS_MAX_NAME_SIZE];		/* query name */
	unsigned short type;			/* question type */
	unsigned short class;			/* query class */
};

/* NOTE: big endian structure */
struct dns_answer_item {
	struct list list;
	char *name;				/* answer name
						 * For SRV type, name also includes service
						 * and protocol value */
	int16_t type;				/* question type */
	int16_t class;				/* query class */
	int32_t ttl;				/* response TTL */
	int16_t priority;			/* SRV type priority */
	int16_t weight;				/* SRV type weight */
	int16_t port;				/* SRV type port */
	int16_t data_len;			/* number of bytes in target below */
	struct sockaddr address;		/* IPv4 or IPv6, network format */
	char *target;				/* Response data: SRV or CNAME type target */
};

struct dns_response_packet {
	struct dns_header header;
	struct list query_list;
	struct list answer_list;
	/* authority and additional_information ignored for now */
};

/*
 * resolvers section and parameters. It is linked to the name servers
 * servers points to it.
 * current resolution are stored in a FIFO list.
 */
struct dns_resolvers {
	struct list list;		/* resolvers list */
	char *id;			/* resolvers unique identifier */
	struct {
		const char *file;	/* file where the section appears */
		int line;		/* line where the section appears */
	} conf;				/* config information */
	struct list nameserver_list;	/* dns server list */
	int count_nameservers;			/* total number of nameservers in a resolvers section */
	int resolve_retries;		/* number of retries before giving up */
	struct {			/* time to: */
		int retry;		/*   wait for a response before retrying */
	} timeout;
	struct {			/* time to hold current data when */
		int valid;		/*   a response is valid */
		int nx;                 /*   a response doesn't exist */
		int timeout;            /*   no answer was delivered */
		int refused;            /*   dns server refused to answer */
		int other;              /*   other dns response errors */
	} hold;
	struct task *t;			/* timeout management */
	struct list curr_resolution;	/* current running resolutions */
	struct eb_root query_ids;	/* tree to quickly lookup/retrieve query ids currently in use */
					/* used by each nameserver, but stored in resolvers since there must */
					/* be a unique relation between an eb_root and an eb_node (resolution) */
};

/*
 * structure describing a name server used during name resolution.
 * A name server belongs to a resolvers section.
 */
struct dns_nameserver {
	struct list list;		/* nameserver chained list */
	char *id;			/* nameserver unique identifier */
	struct {
		const char *file;	/* file where the section appears */
		int line;		/* line where the section appears */
	} conf;				/* config information */
	struct dns_resolvers *resolvers;
	struct dgram_conn *dgram;		/* transport layer */
	struct sockaddr_storage addr;	/* IP address */
	struct {			/* numbers relted to this name server: */
		long int sent;		/* - queries sent */
		long int valid;		/* - valid response */
		long int update;	/* - valid response used to update server's IP */
		long int cname;		/* - CNAME response requiring new resolution */
		long int cname_error;	/* - error when resolving CNAMEs */
		long int any_err;	/* - void response (usually because ANY qtype) */
		long int nx;		/* - NX response */
		long int timeout;	/* - queries which reached timeout */
		long int refused;	/* - queries refused */
		long int other;		/* - other type of response */
		long int invalid;	/* - malformed DNS response */
		long int too_big;	/* - too big response */
		long int outdated;	/* - outdated response (server slower than the other ones) */
		long int truncated;	/* - truncated response */
	} counters;
};

struct dns_options {
	int family_prio;	/* which IP family should the resolver use when both are returned */
	struct {
		int family;
		union {
			struct in_addr in4;
			struct in6_addr in6;
		} addr;
		union {
			struct in_addr in4;
			struct in6_addr in6;
		} mask;
	} pref_net[SRV_MAX_PREF_NET];
	int pref_net_nb;               /* The number of registered prefered networks. */
};

/*
 * resolution structure associated to single server and used to manage name resolution for
 * this server.
 * The only link between the resolution and a nameserver is through the query_id.
 */
struct dns_resolution {
	struct list list;		/* resolution list */
	struct dns_resolvers *resolvers;	/* resolvers section associated to this resolution */
	void *requester;		/* owner of this name resolution */
	int (*requester_cb)(struct dns_resolution *, struct dns_nameserver *, struct dns_response_packet *);
					/* requester callback for valid response */
	int (*requester_error_cb)(struct dns_resolution *, int);
					/* requester callback, for error management */
	char *hostname_dn;		/* server hostname in domain name label format */
	int hostname_dn_len;		/* server domain name label len */
	struct dns_options *opts;       /* IP selection options inherited from the configuration file. */
	unsigned int last_resolution;	/* time of the lastest valid resolution */
	unsigned int last_sent_packet;	/* time of the latest DNS packet sent */
	unsigned int last_status_change;	/* time of the latest DNS resolution status change */
	int query_id;			/* DNS query ID dedicated for this resolution */
	struct eb32_node qid;		/* ebtree query id */
	int query_type;
		/* query type to send. By default DNS_RTYPE_A or DNS_RTYPE_AAAA depending on resolver_family_priority */
	int status;			/* status of the resolution being processed RSLV_STATUS_* */
	int step;			/* */
	int try;			/* current resolution try */
	int try_cname;			/* number of CNAME requests sent */
	int nb_responses;		/* count number of responses received */
};

/* last resolution status code */
enum {
	RSLV_STATUS_NONE	= 0,	/* no resolution occured yet */
	RSLV_STATUS_VALID,		/* no error */
	RSLV_STATUS_INVALID,		/* invalid responses */
	RSLV_STATUS_ERROR,		/* error */
	RSLV_STATUS_NX,			/* NXDOMAIN */
	RSLV_STATUS_REFUSED,		/* server refused our query */
	RSLV_STATUS_TIMEOUT,		/* no response from DNS servers */
	RSLV_STATUS_OTHER,		/* other errors */
};

/* current resolution step */
enum {
	RSLV_STEP_NONE		= 0,	/* nothing happening currently */
	RSLV_STEP_RUNNING,		/* resolution is running */
};

/* return codes after analyzing a DNS response */
enum {
	DNS_RESP_VALID		= 0,	/* valid response */
	DNS_RESP_INVALID,		/* invalid response (various type of errors can trigger it) */
	DNS_RESP_ERROR,			/* DNS error code */
	DNS_RESP_NX_DOMAIN,		/* resolution unsuccessful */
	DNS_RESP_REFUSED,		/* DNS server refused to answer */
	DNS_RESP_ANCOUNT_ZERO,		/* no answers in the response */
	DNS_RESP_WRONG_NAME,		/* response does not match query name */
	DNS_RESP_CNAME_ERROR,		/* error when resolving a CNAME in an atomic response */
	DNS_RESP_TIMEOUT,		/* DNS server has not answered in time */
	DNS_RESP_TRUNCATED,		/* DNS response is truncated */
	DNS_RESP_NO_EXPECTED_RECORD,	/* No expected records were found in the response */
	DNS_RESP_QUERY_COUNT_ERROR,	/* we did not get the expected number of queries in the response */
};

/* return codes after searching an IP in a DNS response buffer, using a family preference */
enum {
	DNS_UPD_NO 		= 1,	/* provided IP was found and preference is matched
					 * OR provided IP found and preference is not matched, but no IP
					 *    matching preference was found */
	DNS_UPD_SRVIP_NOT_FOUND,	/* provided IP not found
					 * OR provided IP found and preference is not match and an IP
					 *    matching preference was found */
	DNS_UPD_CNAME,			/* CNAME without any IP provided in the response */
	DNS_UPD_NAME_ERROR,		/* name in the response did not match the query */
	DNS_UPD_NO_IP_FOUND,		/* no IP could be found in the response */
};

#endif /* _TYPES_DNS_H */
