/*
 * include/types/applet.h
 * This file describes the applet struct and associated constants.
 *
 * Copyright (C) 2000-2015 Willy Tarreau - w@1wt.eu
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

#ifndef _TYPES_APPLET_H
#define _TYPES_APPLET_H

#include <types/hlua.h>
#include <types/obj_type.h>
#include <types/proxy.h>
#include <types/stream.h>
#include <common/buffer.h>
#include <common/chunk.h>
#include <common/config.h>

struct appctx;

/* Applet descriptor */
struct applet {
	enum obj_type obj_type;            /* object type = OBJ_TYPE_APPLET */
	/* 3 unused bytes here */
	char *name;                        /* applet's name to report in logs */
	int (*init)(struct appctx *, struct proxy *px, struct stream *strm);   /* callback to init ressources, may be NULL.
	                                     expect 1 if ok, 0 if an error occurs, -1 if miss data. */
	void (*fct)(struct appctx *);      /* internal I/O handler, may never be NULL */
	void (*release)(struct appctx *);  /* callback to release resources, may be NULL */
	unsigned int timeout;              /* execution timeout. */
};

/* Context of a running applet. */
struct appctx {
	struct list runq;          /* chaining in the applet run queue */
	enum obj_type obj_type;    /* OBJ_TYPE_APPCTX */
	/* 3 unused bytes here */
	unsigned int st0;          /* CLI state for stats, session state for peers */
	unsigned int st1;          /* prompt for stats, session error for peers */
	unsigned int st2;          /* output state for stats, unused by peers  */
	struct applet *applet;     /* applet this context refers to */
	void *owner;               /* pointer to upper layer's entity (eg: stream interface) */
	struct act_rule *rule;     /* rule associated with the applet. */
	int (*io_handler)(struct appctx *appctx);  /* used within the cli_io_handler when st0 = CLI_ST_CALLBACK */
	void (*io_release)(struct appctx *appctx);  /* used within the cli_io_handler when st0 = CLI_ST_CALLBACK,
	                                               if the command is terminated or the session released */
	void *private;
	struct buffer_wait buffer_wait; /* position in the list of objects waiting for a buffer */

	union {
		struct {
			struct proxy *px;
			struct server *sv;
			void *l;
			int scope_str;		/* limit scope to a frontend/backend substring */
			int scope_len;		/* length of the string above in the buffer */
			int px_st;		/* STAT_PX_ST* */
			unsigned int flags;	/* STAT_* */
			int iid, type, sid;	/* proxy id, type and service id if bounding of stats is enabled */
			int st_code;		/* the status code returned by an action */
		} stats;
		struct {
			struct bref bref;	/* back-reference from the session being dumped */
			void *target;		/* session we want to dump, or NULL for all */
			unsigned int uid;	/* if non-null, the uniq_id of the session being dumped */
			int section;		/* section of the session being dumped */
			int pos;		/* last position of the current session's buffer */
		} sess;
		struct {
			int iid;		/* if >= 0, ID of the proxy to filter on */
			struct proxy *px;	/* current proxy being dumped, NULL = not started yet. */
			unsigned int flag;	/* bit0: buffer being dumped, 0 = req, 1 = resp ; bit1=skip req ; bit2=skip resp. */
			unsigned int sid;	/* session ID of error being dumped */
			int ptr;		/* <0: headers, >=0 : text pointer to restart from */
			int bol;		/* pointer to beginning of current line */
		} errors;
		struct {
			void *target;		/* table we want to dump, or NULL for all */
			struct proxy *proxy;	/* table being currently dumped (first if NULL) */
			struct stksess *entry;	/* last entry we were trying to dump (or first if NULL) */
			long long value;	/* value to compare against */
			signed char data_type;	/* type of data to compare, or -1 if none */
			signed char data_op;	/* operator (STD_OP_*) when data_type set */
		} table;
		struct {
			const char *msg;	/* pointer to a persistent message to be returned in PRINT state */
			char *err;        /* pointer to a 'must free' message to be returned in PRINT_FREE state */
		} cli;
		struct {
			void *ptr;              /* multi-purpose pointer for peers */
		} peers;
		struct {
			unsigned int display_flags;
			struct pat_ref *ref;
			struct bref bref;	/* back-reference from the pat_ref_elt being dumped */
			struct pattern_expr *expr;
			struct chunk chunk;
		} map;
#if (defined SSL_CTRL_SET_TLSEXT_TICKET_KEY_CB && TLS_TICKETS_NO > 0)
		struct {
			int dump_all;
			int dump_keys_index;
			struct tls_keys_ref *ref;
		} tlskeys;
#endif
		struct {
			int connected;
			struct hlua_socket *socket;
			struct list wake_on_read;
			struct list wake_on_write;
			int die;
		} hlua;
		struct {
			struct hlua hlua;
			struct task *task;
		} hlua_cli;
		struct {
			struct hlua hlua;
			int flags;
			struct task *task;
		} hlua_apptcp;
		struct {
			struct hlua hlua;
			int left_bytes; /* The max amount of bytes that we can read. */
			int flags;
			int status;
			const char *reason;
			struct task *task;
		} hlua_apphttp;
		struct {
			struct dns_resolvers *ptr;
		} resolvers;
		struct {
			int iid;		/* if >= 0, ID of the proxy to filter on */
			struct proxy *px;	/* current proxy being dumped, NULL = not started yet. */
			struct server *sv;	/* current server being dumped, NULL = not started yet. */
		} server_state;
		struct {
			struct proxy *px;	/* current proxy being dumped, NULL = not started yet. */
		} be;				/* used by "show backends" command */
		struct {
			char **var;
		} env;
		struct {
			struct task *task;
			void        *ctx;
			void        *agent;
			unsigned int version;
			unsigned int max_frame_size;
			struct list  list;
		} spoe;                         /* used by SPOE filter */
	} ctx;					/* used by stats I/O handlers to dump the stats */
};

#endif /* _TYPES_APPLET_H */

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 * End:
 */
