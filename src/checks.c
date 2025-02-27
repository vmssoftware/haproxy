/*
 * Health-checks functions.
 *
 * Copyright 2000-2009 Willy Tarreau <w@1wt.eu>
 * Copyright 2007-2009 Krzysztof Piotr Oledzki <ole@ans.pl>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <common/chunk.h>
#include <common/compat.h>
#include <common/config.h>
#include <common/mini-clist.h>
#include <common/standard.h>
#include <common/time.h>

#include <types/global.h>
#include <types/mailers.h>
#include <types/dns.h>
#include <types/stats.h>

#ifdef USE_OPENSSL
#include <types/ssl_sock.h>
#include <proto/ssl_sock.h>
#endif /* USE_OPENSSL */

#include <proto/backend.h>
#include <proto/checks.h>
#include <proto/stats.h>
#include <proto/fd.h>
#include <proto/log.h>
#include <proto/queue.h>
#include <proto/port_range.h>
#include <proto/proto_http.h>
#include <proto/proto_tcp.h>
#include <proto/protocol.h>
#include <proto/proxy.h>
#include <proto/raw_sock.h>
#include <proto/server.h>
#include <proto/signal.h>
#include <proto/stream_interface.h>
#include <proto/task.h>
#include <proto/log.h>
#include <proto/dns.h>
#include <proto/proto_udp.h>

static int httpchk_expect(struct server *s, int done);
static int tcpcheck_get_step_id(struct check *);
static char * tcpcheck_get_step_comment(struct check *, int);
static void tcpcheck_main(struct connection *);

static const struct check_status check_statuses[HCHK_STATUS_SIZE] = {
	[HCHK_STATUS_UNKNOWN]	= { CHK_RES_UNKNOWN,  "UNK",     "Unknown" },
	[HCHK_STATUS_INI]	= { CHK_RES_UNKNOWN,  "INI",     "Initializing" },
	[HCHK_STATUS_START]	= { /* SPECIAL STATUS*/ },

	/* Below we have finished checks */
	[HCHK_STATUS_CHECKED]	= { CHK_RES_NEUTRAL,  "CHECKED", "No status change" },
	[HCHK_STATUS_HANA]	= { CHK_RES_FAILED,   "HANA",    "Health analyze" },

	[HCHK_STATUS_SOCKERR]	= { CHK_RES_FAILED,   "SOCKERR", "Socket error" },

	[HCHK_STATUS_L4OK]	= { CHK_RES_PASSED,   "L4OK",    "Layer4 check passed" },
	[HCHK_STATUS_L4TOUT]	= { CHK_RES_FAILED,   "L4TOUT",  "Layer4 timeout" },
	[HCHK_STATUS_L4CON]	= { CHK_RES_FAILED,   "L4CON",   "Layer4 connection problem" },

	[HCHK_STATUS_L6OK]	= { CHK_RES_PASSED,   "L6OK",    "Layer6 check passed" },
	[HCHK_STATUS_L6TOUT]	= { CHK_RES_FAILED,   "L6TOUT",  "Layer6 timeout" },
	[HCHK_STATUS_L6RSP]	= { CHK_RES_FAILED,   "L6RSP",   "Layer6 invalid response" },

	[HCHK_STATUS_L7TOUT]	= { CHK_RES_FAILED,   "L7TOUT",  "Layer7 timeout" },
	[HCHK_STATUS_L7RSP]	= { CHK_RES_FAILED,   "L7RSP",   "Layer7 invalid response" },

	[HCHK_STATUS_L57DATA]	= { /* DUMMY STATUS */ },

	[HCHK_STATUS_L7OKD]	= { CHK_RES_PASSED,   "L7OK",    "Layer7 check passed" },
	[HCHK_STATUS_L7OKCD]	= { CHK_RES_CONDPASS, "L7OKC",   "Layer7 check conditionally passed" },
	[HCHK_STATUS_L7STS]	= { CHK_RES_FAILED,   "L7STS",   "Layer7 wrong status" },

	[HCHK_STATUS_PROCERR]	= { CHK_RES_FAILED,   "PROCERR",  "External check error" },
	[HCHK_STATUS_PROCTOUT]	= { CHK_RES_FAILED,   "PROCTOUT", "External check timeout" },
	[HCHK_STATUS_PROCOK]	= { CHK_RES_PASSED,   "PROCOK",   "External check passed" },
};

const struct extcheck_env extcheck_envs[EXTCHK_SIZE] = {
	[EXTCHK_PATH]                   = { "PATH",                   EXTCHK_SIZE_EVAL_INIT },
	[EXTCHK_HAPROXY_PROXY_NAME]     = { "HAPROXY_PROXY_NAME",     EXTCHK_SIZE_EVAL_INIT },
	[EXTCHK_HAPROXY_PROXY_ID]       = { "HAPROXY_PROXY_ID",       EXTCHK_SIZE_EVAL_INIT },
	[EXTCHK_HAPROXY_PROXY_ADDR]     = { "HAPROXY_PROXY_ADDR",     EXTCHK_SIZE_EVAL_INIT },
	[EXTCHK_HAPROXY_PROXY_PORT]     = { "HAPROXY_PROXY_PORT",     EXTCHK_SIZE_EVAL_INIT },
	[EXTCHK_HAPROXY_SERVER_NAME]    = { "HAPROXY_SERVER_NAME",    EXTCHK_SIZE_EVAL_INIT },
	[EXTCHK_HAPROXY_SERVER_ID]      = { "HAPROXY_SERVER_ID",      EXTCHK_SIZE_EVAL_INIT },
	[EXTCHK_HAPROXY_SERVER_ADDR]    = { "HAPROXY_SERVER_ADDR",    EXTCHK_SIZE_EVAL_INIT },
	[EXTCHK_HAPROXY_SERVER_PORT]    = { "HAPROXY_SERVER_PORT",    EXTCHK_SIZE_EVAL_INIT },
	[EXTCHK_HAPROXY_SERVER_MAXCONN] = { "HAPROXY_SERVER_MAXCONN", EXTCHK_SIZE_EVAL_INIT },
	[EXTCHK_HAPROXY_SERVER_CURCONN] = { "HAPROXY_SERVER_CURCONN", EXTCHK_SIZE_ULONG },
};

static const struct analyze_status analyze_statuses[HANA_STATUS_SIZE] = {		/* 0: ignore, 1: error, 2: OK */
	[HANA_STATUS_UNKNOWN]		= { "Unknown",                         { 0, 0 }},

	[HANA_STATUS_L4_OK]		= { "L4 successful connection",        { 2, 0 }},
	[HANA_STATUS_L4_ERR]		= { "L4 unsuccessful connection",      { 1, 1 }},

	[HANA_STATUS_HTTP_OK]		= { "Correct http response",           { 0, 2 }},
	[HANA_STATUS_HTTP_STS]		= { "Wrong http response",             { 0, 1 }},
	[HANA_STATUS_HTTP_HDRRSP]	= { "Invalid http response (headers)", { 0, 1 }},
	[HANA_STATUS_HTTP_RSP]		= { "Invalid http response",           { 0, 1 }},

	[HANA_STATUS_HTTP_READ_ERROR]	= { "Read error (http)",               { 0, 1 }},
	[HANA_STATUS_HTTP_READ_TIMEOUT]	= { "Read timeout (http)",             { 0, 1 }},
	[HANA_STATUS_HTTP_BROKEN_PIPE]	= { "Close from server (http)",        { 0, 1 }},
};

/*
 * Convert check_status code to description
 */
const char *get_check_status_description(short check_status) {

	const char *desc;

	if (check_status < HCHK_STATUS_SIZE)
		desc = check_statuses[check_status].desc;
	else
		desc = NULL;

	if (desc && *desc)
		return desc;
	else
		return check_statuses[HCHK_STATUS_UNKNOWN].desc;
}

/*
 * Convert check_status code to short info
 */
const char *get_check_status_info(short check_status) {

	const char *info;

	if (check_status < HCHK_STATUS_SIZE)
		info = check_statuses[check_status].info;
	else
		info = NULL;

	if (info && *info)
		return info;
	else
		return check_statuses[HCHK_STATUS_UNKNOWN].info;
}

const char *get_analyze_status(short analyze_status) {

	const char *desc;

	if (analyze_status < HANA_STATUS_SIZE)
		desc = analyze_statuses[analyze_status].desc;
	else
		desc = NULL;

	if (desc && *desc)
		return desc;
	else
		return analyze_statuses[HANA_STATUS_UNKNOWN].desc;
}

/* Builds a string containing some information about the health check's result.
 * The output string is allocated from the trash chunks. If the check is NULL,
 * NULL is returned. This is designed to be used when emitting logs about health
 * checks.
 */
static const char *check_reason_string(struct check *check)
{
	struct chunk *msg;

	if (!check)
		return NULL;

	msg = get_trash_chunk();
	chunk_printf(msg, "reason: %s", get_check_status_description(check->status));

	if (check->status >= HCHK_STATUS_L57DATA)
		chunk_appendf(msg, ", code: %d", check->code);

	if (*check->desc) {
		struct chunk src;

		chunk_appendf(msg, ", info: \"");

		chunk_initlen(&src, check->desc, 0, strlen(check->desc));
		chunk_asciiencode(msg, &src, '"');

		chunk_appendf(msg, "\"");
	}

	if (check->duration >= 0)
		chunk_appendf(msg, ", check duration: %ldms", check->duration);

	return msg->str;
}

/*
 * Set check->status, update check->duration and fill check->result with
 * an adequate CHK_RES_* value. The new check->health is computed based
 * on the result.
 *
 * Show information in logs about failed health check if server is UP
 * or succeeded health checks if server is DOWN.
 */
static void set_server_check_status(struct check *check, short status, const char *desc)
{
	struct server *s = check->server;
	short prev_status = check->status;
	int report = 0;

	if (status == HCHK_STATUS_START) {
		check->result = CHK_RES_UNKNOWN;	/* no result yet */
		check->desc[0] = '\0';
		check->start = now;
		return;
	}

	if (!check->status)
		return;

	if (desc && *desc) {
		strncpy(check->desc, desc, HCHK_DESC_LEN-1);
		check->desc[HCHK_DESC_LEN-1] = '\0';
	} else
		check->desc[0] = '\0';

	check->status = status;
	if (check_statuses[status].result)
		check->result = check_statuses[status].result;

	if (status == HCHK_STATUS_HANA)
		check->duration = -1;
	else if (!tv_iszero(&check->start)) {
		/* set_server_check_status() may be called more than once */
		check->duration = tv_ms_elapsed(&check->start, &now);
		tv_zero(&check->start);
	}

	/* no change is expected if no state change occurred */
	if (check->result == CHK_RES_NEUTRAL)
		return;

	report = 0;

	switch (check->result) {
	case CHK_RES_FAILED:
		/* Failure to connect to the agent as a secondary check should not
		 * cause the server to be marked down.
		 */
		if ((!(check->state & CHK_ST_AGENT) ||
		    (check->status >= HCHK_STATUS_L57DATA)) &&
		    (check->health >= check->rise)) {
			s->counters.failed_checks++;
			report = 1;
			check->health--;
			if (check->health < check->rise)
				check->health = 0;
		}
		break;

	case CHK_RES_PASSED:
	case CHK_RES_CONDPASS:	/* "condpass" cannot make the first step but it OK after a "passed" */
		if ((check->health < check->rise + check->fall - 1) &&
		    (check->result == CHK_RES_PASSED || check->health > 0)) {
			report = 1;
			check->health++;

			if (check->health >= check->rise)
				check->health = check->rise + check->fall - 1; /* OK now */
		}

		/* clear consecutive_errors if observing is enabled */
		if (s->onerror)
			s->consecutive_errors = 0;
		break;

	default:
		break;
	}

	if (s->proxy->options2 & PR_O2_LOGHCHKS &&
	    (status != prev_status || report)) {
		chunk_printf(&trash,
		             "%s check for %sserver %s/%s %s%s",
			     (check->state & CHK_ST_AGENT) ? "Agent" : "Health",
		             s->flags & SRV_F_BACKUP ? "backup " : "",
		             s->proxy->id, s->id,
		             (check->result == CHK_RES_CONDPASS) ? "conditionally ":"",
		             (check->result >= CHK_RES_PASSED)   ? "succeeded" : "failed");

		srv_append_status(&trash, s, check_reason_string(check), -1, 0);

		chunk_appendf(&trash, ", status: %d/%d %s",
		             (check->health >= check->rise) ? check->health - check->rise + 1 : check->health,
		             (check->health >= check->rise) ? check->fall : check->rise,
			     (check->health >= check->rise) ? (s->uweight ? "UP" : "DRAIN") : "DOWN");

		Warning("%s.\n", trash.str);
		send_log(s->proxy, LOG_NOTICE, "%s.\n", trash.str);
		send_email_alert(s, LOG_INFO, "%s", trash.str);
	}
}

/* Marks the check <check>'s server down if the current check is already failed
 * and the server is not down yet nor in maintenance.
 */
static void check_notify_failure(struct check *check)
{
	struct server *s = check->server;

	/* The agent secondary check should only cause a server to be marked
	 * as down if check->status is HCHK_STATUS_L7STS, which indicates
	 * that the agent returned "fail", "stopped" or "down".
	 * The implication here is that failure to connect to the agent
	 * as a secondary check should not cause the server to be marked
	 * down. */
	if ((check->state & CHK_ST_AGENT) && check->status != HCHK_STATUS_L7STS)
		return;

	if (check->health > 0)
		return;

	/* We only report a reason for the check if we did not do so previously */
	srv_set_stopped(s, (!s->track && !(s->proxy->options2 & PR_O2_LOGHCHKS)) ? check_reason_string(check) : NULL);
}

/* Marks the check <check> as valid and tries to set its server up, provided
 * it isn't in maintenance, it is not tracking a down server and other checks
 * comply. The rule is simple : by default, a server is up, unless any of the
 * following conditions is true :
 *   - health check failed (check->health < rise)
 *   - agent check failed (agent->health < rise)
 *   - the server tracks a down server (track && track->state == STOPPED)
 * Note that if the server has a slowstart, it will switch to STARTING instead
 * of RUNNING. Also, only the health checks support the nolb mode, so the
 * agent's success may not take the server out of this mode.
 */
static void check_notify_success(struct check *check)
{
	struct server *s = check->server;

	if (s->admin & SRV_ADMF_MAINT)
		return;

	if (s->track && s->track->state == SRV_ST_STOPPED)
		return;

	if ((s->check.state & CHK_ST_ENABLED) && (s->check.health < s->check.rise))
		return;

	if ((s->agent.state & CHK_ST_ENABLED) && (s->agent.health < s->agent.rise))
		return;

	if ((check->state & CHK_ST_AGENT) && s->state == SRV_ST_STOPPING)
		return;

	srv_set_running(s, (!s->track && !(s->proxy->options2 & PR_O2_LOGHCHKS)) ? check_reason_string(check) : NULL);
}

/* Marks the check <check> as valid and tries to set its server into stopping mode
 * if it was running or starting, and provided it isn't in maintenance and other
 * checks comply. The conditions for the server to be marked in stopping mode are
 * the same as for it to be turned up. Also, only the health checks support the
 * nolb mode.
 */
static void check_notify_stopping(struct check *check)
{
	struct server *s = check->server;

	if (s->admin & SRV_ADMF_MAINT)
		return;

	if (check->state & CHK_ST_AGENT)
		return;

	if (s->track && s->track->state == SRV_ST_STOPPED)
		return;

	if ((s->check.state & CHK_ST_ENABLED) && (s->check.health < s->check.rise))
		return;

	if ((s->agent.state & CHK_ST_ENABLED) && (s->agent.health < s->agent.rise))
		return;

	srv_set_stopping(s, (!s->track && !(s->proxy->options2 & PR_O2_LOGHCHKS)) ? check_reason_string(check) : NULL);
}

/* note: use health_adjust() only, which first checks that the observe mode is
 * enabled.
 */
void __health_adjust(struct server *s, short status)
{
	int failed;
	int expire;

	if (s->observe >= HANA_OBS_SIZE)
		return;

	if (status >= HANA_STATUS_SIZE || !analyze_statuses[status].desc)
		return;

	switch (analyze_statuses[status].lr[s->observe - 1]) {
		case 1:
			failed = 1;
			break;

		case 2:
			failed = 0;
			break;

		default:
			return;
	}

	if (!failed) {
		/* good: clear consecutive_errors */
		s->consecutive_errors = 0;
		return;
	}

	s->consecutive_errors++;

	if (s->consecutive_errors < s->consecutive_errors_limit)
		return;

	chunk_printf(&trash, "Detected %d consecutive errors, last one was: %s",
	             s->consecutive_errors, get_analyze_status(status));

	switch (s->onerror) {
		case HANA_ONERR_FASTINTER:
		/* force fastinter - nothing to do here as all modes force it */
			break;

		case HANA_ONERR_SUDDTH:
		/* simulate a pre-fatal failed health check */
			if (s->check.health > s->check.rise)
				s->check.health = s->check.rise + 1;

			/* no break - fall through */

		case HANA_ONERR_FAILCHK:
		/* simulate a failed health check */
			set_server_check_status(&s->check, HCHK_STATUS_HANA, trash.str);
			check_notify_failure(&s->check);
			break;

		case HANA_ONERR_MARKDWN:
		/* mark server down */
			s->check.health = s->check.rise;
			set_server_check_status(&s->check, HCHK_STATUS_HANA, trash.str);
			check_notify_failure(&s->check);
			break;

		default:
			/* write a warning? */
			break;
	}

	s->consecutive_errors = 0;
	s->counters.failed_hana++;

	if (s->check.fastinter) {
		expire = tick_add(now_ms, MS_TO_TICKS(s->check.fastinter));
		if (s->check.task->expire > expire) {
			s->check.task->expire = expire;
			/* requeue check task with new expire */
			task_queue(s->check.task);
		}
	}
}

static int httpchk_build_status_header(struct server *s, char *buffer, int size)
{
	int sv_state;
	int ratio;
	int hlen = 0;
	char addr[46];
	char port[6];
	const char *srv_hlt_st[7] = { "DOWN", "DOWN %d/%d",
				      "UP %d/%d", "UP",
				      "NOLB %d/%d", "NOLB",
				      "no check" };

	memcpy(buffer + hlen, "X-Haproxy-Server-State: ", 24);
	hlen += 24;

	if (!(s->check.state & CHK_ST_ENABLED))
		sv_state = 6;
	else if (s->state != SRV_ST_STOPPED) {
		if (s->check.health == s->check.rise + s->check.fall - 1)
			sv_state = 3; /* UP */
		else
			sv_state = 2; /* going down */

		if (s->state == SRV_ST_STOPPING)
			sv_state += 2;
	} else {
		if (s->check.health)
			sv_state = 1; /* going up */
		else
			sv_state = 0; /* DOWN */
	}

	hlen += snprintf(buffer + hlen, size - hlen,
			     srv_hlt_st[sv_state],
			     (s->state != SRV_ST_STOPPED) ? (s->check.health - s->check.rise + 1) : (s->check.health),
			     (s->state != SRV_ST_STOPPED) ? (s->check.fall) : (s->check.rise));

	addr_to_str(&s->addr, addr, sizeof(addr));
	if (s->addr.ss_family == AF_INET || s->addr.ss_family == AF_INET6)
		snprintf(port, sizeof(port), "%u", s->svc_port);
	else
		*port = 0;

	hlen += snprintf(buffer + hlen,  size - hlen, "; address=%s; port=%s; name=%s/%s; node=%s; weight=%d/%d; scur=%d/%d; qcur=%d",
			     addr, port, s->proxy->id, s->id,
			     global.node,
			     (s->eweight * s->proxy->lbprm.wmult + s->proxy->lbprm.wdiv - 1) / s->proxy->lbprm.wdiv,
			     (s->proxy->lbprm.tot_weight * s->proxy->lbprm.wmult + s->proxy->lbprm.wdiv - 1) / s->proxy->lbprm.wdiv,
			     s->cur_sess, s->proxy->beconn - s->proxy->nbpend,
			     s->nbpend);

	if ((s->state == SRV_ST_STARTING) &&
	    now.tv_sec < s->last_change + s->slowstart &&
	    now.tv_sec >= s->last_change) {
		ratio = MAX(1, 100 * (now.tv_sec - s->last_change) / s->slowstart);
		hlen += snprintf(buffer + hlen, size - hlen, "; throttle=%d%%", ratio);
	}

	buffer[hlen++] = '\r';
	buffer[hlen++] = '\n';

	return hlen;
}

/* Check the connection. If an error has already been reported or the socket is
 * closed, keep errno intact as it is supposed to contain the valid error code.
 * If no error is reported, check the socket's error queue using getsockopt().
 * Warning, this must be done only once when returning from poll, and never
 * after an I/O error was attempted, otherwise the error queue might contain
 * inconsistent errors. If an error is detected, the CO_FL_ERROR is set on the
 * socket. Returns non-zero if an error was reported, zero if everything is
 * clean (including a properly closed socket).
 */
static int retrieve_errno_from_socket(struct connection *conn)
{
	int skerr;
	socklen_t lskerr = sizeof(skerr);

#ifdef __VMS
	if (conn->flags & CO_FL_ERROR && ((errno && errno != EAGAIN && errno != EWOULDBLOCK) || !conn->ctrl))
		return 1;
#else
	if (conn->flags & CO_FL_ERROR && ((errno && errno != EAGAIN) || !conn->ctrl))
		return 1;
#endif

	if (!conn_ctrl_ready(conn))
		return 0;

	if (getsockopt(conn->t.sock.fd, SOL_SOCKET, SO_ERROR, &skerr, &lskerr) == 0)
		errno = skerr;

#ifdef __VMS
	if (errno == EAGAIN || errno == EWOULDBLOCK)
		errno = 0;
#else
	if (errno == EAGAIN)
		errno = 0;
#endif

	if (!errno) {
		/* we could not retrieve an error, that does not mean there is
		 * none. Just don't change anything and only report the prior
		 * error if any.
		 */
		if (conn->flags & CO_FL_ERROR)
			return 1;
		else
			return 0;
	}

	conn->flags |= CO_FL_ERROR | CO_FL_SOCK_WR_SH | CO_FL_SOCK_RD_SH;
	return 1;
}

/* Try to collect as much information as possible on the connection status,
 * and adjust the server status accordingly. It may make use of <errno_bck>
 * if non-null when the caller is absolutely certain of its validity (eg:
 * checked just after a syscall). If the caller doesn't have a valid errno,
 * it can pass zero, and retrieve_errno_from_socket() will be called to try
 * to extract errno from the socket. If no error is reported, it will consider
 * the <expired> flag. This is intended to be used when a connection error was
 * reported in conn->flags or when a timeout was reported in <expired>. The
 * function takes care of not updating a server status which was already set.
 * All situations where at least one of <expired> or CO_FL_ERROR are set
 * produce a status.
 */
static void chk_report_conn_err(struct connection *conn, int errno_bck, int expired)
{
	struct check *check = conn->owner;
	const char *err_msg;
	struct chunk *chk;
	int step;
	char *comment;

	if (check->result != CHK_RES_UNKNOWN)
		return;

	errno = errno_bck;
#ifdef __VMS
	if (!errno || errno == EAGAIN || errno == EWOULDBLOCK)
		retrieve_errno_from_socket(conn);
#else
	if (!errno || errno == EAGAIN)
		retrieve_errno_from_socket(conn);
#endif

	if (!(conn->flags & CO_FL_ERROR) && !expired)
		return;

	/* we'll try to build a meaningful error message depending on the
	 * context of the error possibly present in conn->err_code, and the
	 * socket error possibly collected above. This is useful to know the
	 * exact step of the L6 layer (eg: SSL handshake).
	 */
	chk = get_trash_chunk();

	if (check->type == PR_O2_TCPCHK_CHK) {
		step = tcpcheck_get_step_id(check);
		if (!step)
			chunk_printf(chk, " at initial connection step of tcp-check");
		else {
			chunk_printf(chk, " at step %d of tcp-check", step);
			/* we were looking for a string */
			if (check->last_started_step && check->last_started_step->action == TCPCHK_ACT_CONNECT) {
				if (check->last_started_step->port)
					chunk_appendf(chk, " (connect port %d)" ,check->last_started_step->port);
				else
					chunk_appendf(chk, " (connect)");
			}
			else if (check->last_started_step && check->last_started_step->action == TCPCHK_ACT_EXPECT) {
				if (check->last_started_step->string)
					chunk_appendf(chk, " (expect string '%s')", check->last_started_step->string);
				else if (check->last_started_step->expect_regex)
					chunk_appendf(chk, " (expect regex)");
			}
			else if (check->last_started_step && check->last_started_step->action == TCPCHK_ACT_SEND) {
				chunk_appendf(chk, " (send)");
			}

			comment = tcpcheck_get_step_comment(check, step);
			if (comment)
				chunk_appendf(chk, " comment: '%s'", comment);
		}
	}

	if (conn->err_code) {
#ifdef __VMS
		if (errno && errno != EAGAIN && errno != EWOULDBLOCK)
#else
		if (errno && errno != EAGAIN)
#endif
			chunk_printf(&trash, "%s (%s)%s", conn_err_code_str(conn), strerror(errno), chk->str);
		else
			chunk_printf(&trash, "%s%s", conn_err_code_str(conn), chk->str);
		err_msg = trash.str;
	}
	else {
#ifdef __VMS
		if (errno && errno != EAGAIN && errno != EWOULDBLOCK) {
#else
		if (errno && errno != EAGAIN) {
#endif
			chunk_printf(&trash, "%s%s", strerror(errno), chk->str);
			err_msg = trash.str;
		}
		else {
			err_msg = chk->str;
		}
	}

       if (check->state & CHK_ST_PORT_MISS) {
		/* NOTE: this is reported after <fall> tries */
		chunk_printf(chk, "No port available for the TCP connection");
		set_server_check_status(check, HCHK_STATUS_SOCKERR, err_msg);
	}

	if ((conn->flags & (CO_FL_CONNECTED|CO_FL_WAIT_L4_CONN)) == CO_FL_WAIT_L4_CONN) {
		/* L4 not established (yet) */
		if (conn->flags & CO_FL_ERROR)
			set_server_check_status(check, HCHK_STATUS_L4CON, err_msg);
		else if (expired)
			set_server_check_status(check, HCHK_STATUS_L4TOUT, err_msg);

		/*
		 * might be due to a server IP change.
		 * Let's trigger a DNS resolution if none are currently running.
		 */
		if ((check->server->resolution)	&& (check->server->resolution->step == RSLV_STEP_NONE))
			trigger_resolution(check->server);

	}
	else if ((conn->flags & (CO_FL_CONNECTED|CO_FL_WAIT_L6_CONN)) == CO_FL_WAIT_L6_CONN) {
		/* L6 not established (yet) */
		if (conn->flags & CO_FL_ERROR)
			set_server_check_status(check, HCHK_STATUS_L6RSP, err_msg);
		else if (expired)
			set_server_check_status(check, HCHK_STATUS_L6TOUT, err_msg);
	}
	else if (conn->flags & CO_FL_ERROR) {
		/* I/O error after connection was established and before we could diagnose */
		set_server_check_status(check, HCHK_STATUS_SOCKERR, err_msg);
	}
	else if (expired) {
		/* connection established but expired check */
		if (check->type == PR_O2_SSL3_CHK)
			set_server_check_status(check, HCHK_STATUS_L6TOUT, err_msg);
		else	/* HTTP, SMTP, ... */
			set_server_check_status(check, HCHK_STATUS_L7TOUT, err_msg);
	}

	return;
}

/*
 * This function is used only for server health-checks. It handles
 * the connection acknowledgement. If the proxy requires L7 health-checks,
 * it sends the request. In other cases, it calls set_server_check_status()
 * to set check->status, check->duration and check->result.
 */
static void event_srv_chk_w(struct connection *conn)
{
	struct check *check = conn->owner;
	struct server *s = check->server;
	struct task *t = check->task;

	if (unlikely(check->result == CHK_RES_FAILED))
		goto out_wakeup;

	if (conn->flags & CO_FL_HANDSHAKE)
		return;

	if (retrieve_errno_from_socket(conn)) {
		chk_report_conn_err(conn, errno, 0);
		__conn_data_stop_both(conn);
		goto out_wakeup;
	}

	if (conn->flags & (CO_FL_SOCK_WR_SH | CO_FL_DATA_WR_SH)) {
		/* if the output is closed, we can't do anything */
		conn->flags |= CO_FL_ERROR;
		chk_report_conn_err(conn, 0, 0);
		goto out_wakeup;
	}

	/* here, we know that the connection is established. That's enough for
	 * a pure TCP check.
	 */
	if (!check->type)
		goto out_wakeup;

	if (check->type == PR_O2_TCPCHK_CHK) {
		tcpcheck_main(conn);
		return;
	}

	if (check->bo->o) {
		conn->xprt->snd_buf(conn, check->bo, 0);
		if (conn->flags & CO_FL_ERROR) {
			chk_report_conn_err(conn, errno, 0);
			__conn_data_stop_both(conn);
			goto out_wakeup;
		}
		if (check->bo->o)
			return;
	}

	/* full request sent, we allow up to <timeout.check> if nonzero for a response */
	if (s->proxy->timeout.check) {
		t->expire = tick_add_ifset(now_ms, s->proxy->timeout.check);
		task_queue(t);
	}
	goto out_nowake;

 out_wakeup:
	task_wakeup(t, TASK_WOKEN_IO);
 out_nowake:
	__conn_data_stop_send(conn);   /* nothing more to write */
}

/*
 * This function is used only for server health-checks. It handles the server's
 * reply to an HTTP request, SSL HELLO or MySQL client Auth. It calls
 * set_server_check_status() to update check->status, check->duration
 * and check->result.

 * The set_server_check_status function is called with HCHK_STATUS_L7OKD if
 * an HTTP server replies HTTP 2xx or 3xx (valid responses), if an SMTP server
 * returns 2xx, HCHK_STATUS_L6OK if an SSL server returns at least 5 bytes in
 * response to an SSL HELLO (the principle is that this is enough to
 * distinguish between an SSL server and a pure TCP relay). All other cases will
 * call it with a proper error status like HCHK_STATUS_L7STS, HCHK_STATUS_L6RSP,
 * etc.
 */
static void event_srv_chk_r(struct connection *conn)
{
	struct check *check = conn->owner;
	struct server *s = check->server;
	struct task *t = check->task;
	char *desc;
	int done;
	unsigned short msglen;

	if (unlikely(check->result == CHK_RES_FAILED))
		goto out_wakeup;

	if (conn->flags & CO_FL_HANDSHAKE)
		return;

	if (check->type == PR_O2_TCPCHK_CHK) {
		tcpcheck_main(conn);
		return;
	}

	/* Warning! Linux returns EAGAIN on SO_ERROR if data are still available
	 * but the connection was closed on the remote end. Fortunately, recv still
	 * works correctly and we don't need to do the getsockopt() on linux.
	 */

	/* Set buffer to point to the end of the data already read, and check
	 * that there is free space remaining. If the buffer is full, proceed
	 * with running the checks without attempting another socket read.
	 */

	done = 0;

	conn->xprt->rcv_buf(conn, check->bi, check->bi->size);
	if (conn->flags & (CO_FL_ERROR | CO_FL_SOCK_RD_SH | CO_FL_DATA_RD_SH)) {
		done = 1;
		if ((conn->flags & CO_FL_ERROR) && !check->bi->i) {
			/* Report network errors only if we got no other data. Otherwise
			 * we'll let the upper layers decide whether the response is OK
			 * or not. It is very common that an RST sent by the server is
			 * reported as an error just after the last data chunk.
			 */
			chk_report_conn_err(conn, errno, 0);
			goto out_wakeup;
		}
	}


	/* Intermediate or complete response received.
	 * Terminate string in check->bi->data buffer.
	 */
	if (check->bi->i < check->bi->size)
		check->bi->data[check->bi->i] = '\0';
	else {
		check->bi->data[check->bi->i - 1] = '\0';
		done = 1; /* buffer full, don't wait for more data */
	}

	/* Run the checks... */
	switch (check->type) {
	case PR_O2_HTTP_CHK:
		if (!done && check->bi->i < strlen("HTTP/1.0 000\r"))
			goto wait_more_data;

		/* Check if the server speaks HTTP 1.X */
		if ((check->bi->i < strlen("HTTP/1.0 000\r")) ||
		    (memcmp(check->bi->data, "HTTP/1.", 7) != 0 ||
		    (*(check->bi->data + 12) != ' ' && *(check->bi->data + 12) != '\r')) ||
		    !isdigit((unsigned char) *(check->bi->data + 9)) || !isdigit((unsigned char) *(check->bi->data + 10)) ||
		    !isdigit((unsigned char) *(check->bi->data + 11))) {
			cut_crlf(check->bi->data);
			set_server_check_status(check, HCHK_STATUS_L7RSP, check->bi->data);

			goto out_wakeup;
		}

		check->code = str2uic(check->bi->data + 9);
		desc = ltrim(check->bi->data + 12, ' ');

		if ((s->proxy->options & PR_O_DISABLE404) &&
			 (s->state != SRV_ST_STOPPED) && (check->code == 404)) {
			/* 404 may be accepted as "stopping" only if the server was up */
			cut_crlf(desc);
			set_server_check_status(check, HCHK_STATUS_L7OKCD, desc);
		}
		else if (s->proxy->options2 & PR_O2_EXP_TYPE) {
			/* Run content verification check... We know we have at least 13 chars */
			if (!httpchk_expect(s, done))
				goto wait_more_data;
		}
		/* check the reply : HTTP/1.X 2xx and 3xx are OK */
		else if (*(check->bi->data + 9) == '2' || *(check->bi->data + 9) == '3') {
			cut_crlf(desc);
			set_server_check_status(check,  HCHK_STATUS_L7OKD, desc);
		}
		else {
			cut_crlf(desc);
			set_server_check_status(check, HCHK_STATUS_L7STS, desc);
		}
		break;

	case PR_O2_SSL3_CHK:
		if (!done && check->bi->i < 5)
			goto wait_more_data;

		/* Check for SSLv3 alert or handshake */
		if ((check->bi->i >= 5) && (*check->bi->data == 0x15 || *check->bi->data == 0x16))
			set_server_check_status(check, HCHK_STATUS_L6OK, NULL);
		else
			set_server_check_status(check, HCHK_STATUS_L6RSP, NULL);
		break;

	case PR_O2_SMTP_CHK:
		if (!done && check->bi->i < strlen("000\r"))
			goto wait_more_data;

		/* Check if the server speaks SMTP */
		if ((check->bi->i < strlen("000\r")) ||
		    (*(check->bi->data + 3) != ' ' && *(check->bi->data + 3) != '\r') ||
		    !isdigit((unsigned char) *check->bi->data) || !isdigit((unsigned char) *(check->bi->data + 1)) ||
		    !isdigit((unsigned char) *(check->bi->data + 2))) {
			cut_crlf(check->bi->data);
			set_server_check_status(check, HCHK_STATUS_L7RSP, check->bi->data);

			goto out_wakeup;
		}

		check->code = str2uic(check->bi->data);

		desc = ltrim(check->bi->data + 3, ' ');
		cut_crlf(desc);

		/* Check for SMTP code 2xx (should be 250) */
		if (*check->bi->data == '2')
			set_server_check_status(check, HCHK_STATUS_L7OKD, desc);
		else
			set_server_check_status(check, HCHK_STATUS_L7STS, desc);
		break;

	case PR_O2_LB_AGENT_CHK: {
		int status = HCHK_STATUS_CHECKED;
		const char *hs = NULL; /* health status      */
		const char *as = NULL; /* admin status */
		const char *ps = NULL; /* performance status */
		const char *cs = NULL; /* maxconn */
		const char *err = NULL; /* first error to report */
		const char *wrn = NULL; /* first warning to report */
		char *cmd, *p;

		/* We're getting an agent check response. The agent could
		 * have been disabled in the mean time with a long check
		 * still pending. It is important that we ignore the whole
		 * response.
		 */
		if (!(check->server->agent.state & CHK_ST_ENABLED))
			break;

		/* The agent supports strings made of a single line ended by the
		 * first CR ('\r') or LF ('\n'). This line is composed of words
		 * delimited by spaces (' '), tabs ('\t'), or commas (','). The
		 * line may optionally contained a description of a state change
		 * after a sharp ('#'), which is only considered if a health state
		 * is announced.
		 *
		 * Words may be composed of :
		 *   - a numeric weight suffixed by the percent character ('%').
		 *   - a health status among "up", "down", "stopped", and "fail".
		 *   - an admin status among "ready", "drain", "maint".
		 *
		 * These words may appear in any order. If multiple words of the
		 * same category appear, the last one wins.
		 */

		p = check->bi->data;
		while (*p && *p != '\n' && *p != '\r')
			p++;

		if (!*p) {
			if (!done)
				goto wait_more_data;

			/* at least inform the admin that the agent is mis-behaving */
			set_server_check_status(check, check->status, "Ignoring incomplete line from agent");
			break;
		}

		*p = 0;
		cmd = check->bi->data;

		while (*cmd) {
			/* look for next word */
			if (*cmd == ' ' || *cmd == '\t' || *cmd == ',') {
				cmd++;
				continue;
			}

			if (*cmd == '#') {
				/* this is the beginning of a health status description,
				 * skip the sharp and blanks.
				 */
				cmd++;
				while (*cmd == '\t' || *cmd == ' ')
					cmd++;
				break;
			}

			/* find the end of the word so that we have a null-terminated
			 * word between <cmd> and <p>.
			 */
			p = cmd + 1;
			while (*p && *p != '\t' && *p != ' ' && *p != '\n' && *p != ',')
				p++;
			if (*p)
				*p++ = 0;

			/* first, health statuses */
			if (strcasecmp(cmd, "up") == 0) {
				check->health = check->rise + check->fall - 1;
				status = HCHK_STATUS_L7OKD;
				hs = cmd;
			}
			else if (strcasecmp(cmd, "down") == 0) {
				check->health = 0;
				status = HCHK_STATUS_L7STS;
				hs = cmd;
			}
			else if (strcasecmp(cmd, "stopped") == 0) {
				check->health = 0;
				status = HCHK_STATUS_L7STS;
				hs = cmd;
			}
			else if (strcasecmp(cmd, "fail") == 0) {
				check->health = 0;
				status = HCHK_STATUS_L7STS;
				hs = cmd;
			}
			/* admin statuses */
			else if (strcasecmp(cmd, "ready") == 0) {
				as = cmd;
			}
			else if (strcasecmp(cmd, "drain") == 0) {
				as = cmd;
			}
			else if (strcasecmp(cmd, "maint") == 0) {
				as = cmd;
			}
			/* try to parse a weight here and keep the last one */
			else if (isdigit((unsigned char)*cmd) && strchr(cmd, '%') != NULL) {
				ps = cmd;
			}
			/* try to parse a maxconn here */
			else if (strncasecmp(cmd, "maxconn:", strlen("maxconn:")) == 0) {
				cs = cmd;
			}
			else {
				/* keep a copy of the first error */
				if (!err)
					err = cmd;
			}
			/* skip to next word */
			cmd = p;
		}
		/* here, cmd points either to \0 or to the beginning of a
		 * description. Skip possible leading spaces.
		 */
		while (*cmd == ' ' || *cmd == '\n')
			cmd++;

		/* First, update the admin status so that we avoid sending other
		 * possibly useless warnings and can also update the health if
		 * present after going back up.
		 */
		if (as) {
			if (strcasecmp(as, "drain") == 0)
				srv_adm_set_drain(check->server);
			else if (strcasecmp(as, "maint") == 0)
				srv_adm_set_maint(check->server);
			else
				srv_adm_set_ready(check->server);
		}

		/* now change weights */
		if (ps) {
			const char *msg;

			msg = server_parse_weight_change_request(s, ps);
			if (!wrn || !*wrn)
				wrn = msg;
		}

		if (cs) {
			const char *msg;

			cs += strlen("maxconn:");

			msg = server_parse_maxconn_change_request(s, cs);
			if (!wrn || !*wrn)
				wrn = msg;
		}

		/* and finally health status */
		if (hs) {
			/* We'll report some of the warnings and errors we have
			 * here. Down reports are critical, we leave them untouched.
			 * Lack of report, or report of 'UP' leaves the room for
			 * ERR first, then WARN.
			 */
			const char *msg = cmd;
			struct chunk *t;

			if (!*msg || status == HCHK_STATUS_L7OKD) {
				if (err && *err)
					msg = err;
				else if (wrn && *wrn)
					msg = wrn;
			}

			t = get_trash_chunk();
			chunk_printf(t, "via agent : %s%s%s%s",
				     hs, *msg ? " (" : "",
				     msg, *msg ? ")" : "");

			set_server_check_status(check, status, t->str);
		}
		else if (err && *err) {
			/* No status change but we'd like to report something odd.
			 * Just report the current state and copy the message.
			 */
			chunk_printf(&trash, "agent reports an error : %s", err);
			set_server_check_status(check, status/*check->status*/, trash.str);

		}
		else if (wrn && *wrn) {
			/* No status change but we'd like to report something odd.
			 * Just report the current state and copy the message.
			 */
			chunk_printf(&trash, "agent warns : %s", wrn);
			set_server_check_status(check, status/*check->status*/, trash.str);
		}
		else
			set_server_check_status(check, status, NULL);
		break;
	}

	case PR_O2_PGSQL_CHK:
		if (!done && check->bi->i < 9)
			goto wait_more_data;

		if (check->bi->data[0] == 'R') {
			set_server_check_status(check, HCHK_STATUS_L7OKD, "PostgreSQL server is ok");
		}
		else {
			if ((check->bi->data[0] == 'E') && (check->bi->data[5]!=0) && (check->bi->data[6]!=0))
				desc = &check->bi->data[6];
			else
				desc = "PostgreSQL unknown error";

			set_server_check_status(check, HCHK_STATUS_L7STS, desc);
		}
		break;

	case PR_O2_REDIS_CHK:
		if (!done && check->bi->i < 7)
			goto wait_more_data;

		if (strcmp(check->bi->data, "+PONG\r\n") == 0) {
			set_server_check_status(check, HCHK_STATUS_L7OKD, "Redis server is ok");
		}
		else {
			set_server_check_status(check, HCHK_STATUS_L7STS, check->bi->data);
		}
		break;

	case PR_O2_MYSQL_CHK:
		if (!done && check->bi->i < 5)
			goto wait_more_data;

		if (s->proxy->check_len == 0) { // old mode
			if (*(check->bi->data + 4) != '\xff') {
				/* We set the MySQL Version in description for information purpose
				 * FIXME : it can be cool to use MySQL Version for other purpose,
				 * like mark as down old MySQL server.
				 */
				if (check->bi->i > 51) {
					desc = ltrim(check->bi->data + 5, ' ');
					set_server_check_status(check, HCHK_STATUS_L7OKD, desc);
				}
				else {
					if (!done)
						goto wait_more_data;
					/* it seems we have a OK packet but without a valid length,
					 * it must be a protocol error
					 */
					set_server_check_status(check, HCHK_STATUS_L7RSP, check->bi->data);
				}
			}
			else {
				/* An error message is attached in the Error packet */
				desc = ltrim(check->bi->data + 7, ' ');
				set_server_check_status(check, HCHK_STATUS_L7STS, desc);
			}
		} else {
			unsigned int first_packet_len = ((unsigned int) *check->bi->data) +
			                                (((unsigned int) *(check->bi->data + 1)) << 8) +
			                                (((unsigned int) *(check->bi->data + 2)) << 16);

			if (check->bi->i == first_packet_len + 4) {
				/* MySQL Error packet always begin with field_count = 0xff */
				if (*(check->bi->data + 4) != '\xff') {
					/* We have only one MySQL packet and it is a Handshake Initialization packet
					* but we need to have a second packet to know if it is alright
					*/
					if (!done && check->bi->i < first_packet_len + 5)
						goto wait_more_data;
				}
				else {
					/* We have only one packet and it is an Error packet,
					* an error message is attached, so we can display it
					*/
					desc = &check->bi->data[7];
					//Warning("onlyoneERR: %s\n", desc);
					set_server_check_status(check, HCHK_STATUS_L7STS, desc);
				}
			} else if (check->bi->i > first_packet_len + 4) {
				unsigned int second_packet_len = ((unsigned int) *(check->bi->data + first_packet_len + 4)) +
				                                 (((unsigned int) *(check->bi->data + first_packet_len + 5)) << 8) +
				                                 (((unsigned int) *(check->bi->data + first_packet_len + 6)) << 16);

				if (check->bi->i == first_packet_len + 4 + second_packet_len + 4 ) {
					/* We have 2 packets and that's good */
					/* Check if the second packet is a MySQL Error packet or not */
					if (*(check->bi->data + first_packet_len + 8) != '\xff') {
						/* No error packet */
						/* We set the MySQL Version in description for information purpose */
						desc = &check->bi->data[5];
						//Warning("2packetOK: %s\n", desc);
						set_server_check_status(check, HCHK_STATUS_L7OKD, desc);
					}
					else {
						/* An error message is attached in the Error packet
						* so we can display it ! :)
						*/
						desc = &check->bi->data[first_packet_len+11];
						//Warning("2packetERR: %s\n", desc);
						set_server_check_status(check, HCHK_STATUS_L7STS, desc);
					}
				}
			}
			else {
				if (!done)
					goto wait_more_data;
				/* it seems we have a Handshake Initialization packet but without a valid length,
				 * it must be a protocol error
				 */
				desc = &check->bi->data[5];
				//Warning("protoerr: %s\n", desc);
				set_server_check_status(check, HCHK_STATUS_L7RSP, desc);
			}
		}
		break;

	case PR_O2_LDAP_CHK:
		if (!done && check->bi->i < 14)
			goto wait_more_data;

		/* Check if the server speaks LDAP (ASN.1/BER)
		 * http://en.wikipedia.org/wiki/Basic_Encoding_Rules
		 * http://tools.ietf.org/html/rfc4511
		 */

		/* http://tools.ietf.org/html/rfc4511#section-4.1.1
		 *   LDAPMessage: 0x30: SEQUENCE
		 */
		if ((check->bi->i < 14) || (*(check->bi->data) != '\x30')) {
			set_server_check_status(check, HCHK_STATUS_L7RSP, "Not LDAPv3 protocol");
		}
		else {
			 /* size of LDAPMessage */
			msglen = (*(check->bi->data + 1) & 0x80) ? (*(check->bi->data + 1) & 0x7f) : 0;

			/* http://tools.ietf.org/html/rfc4511#section-4.2.2
			 *   messageID: 0x02 0x01 0x01: INTEGER 1
			 *   protocolOp: 0x61: bindResponse
			 */
			if ((msglen > 2) ||
			    (memcmp(check->bi->data + 2 + msglen, "\x02\x01\x01\x61", 4) != 0)) {
				set_server_check_status(check, HCHK_STATUS_L7RSP, "Not LDAPv3 protocol");

				goto out_wakeup;
			}

			/* size of bindResponse */
			msglen += (*(check->bi->data + msglen + 6) & 0x80) ? (*(check->bi->data + msglen + 6) & 0x7f) : 0;

			/* http://tools.ietf.org/html/rfc4511#section-4.1.9
			 *   ldapResult: 0x0a 0x01: ENUMERATION
			 */
			if ((msglen > 4) ||
			    (memcmp(check->bi->data + 7 + msglen, "\x0a\x01", 2) != 0)) {
				set_server_check_status(check, HCHK_STATUS_L7RSP, "Not LDAPv3 protocol");

				goto out_wakeup;
			}

			/* http://tools.ietf.org/html/rfc4511#section-4.1.9
			 *   resultCode
			 */
			check->code = *(check->bi->data + msglen + 9);
			if (check->code) {
				set_server_check_status(check, HCHK_STATUS_L7STS, "See RFC: http://tools.ietf.org/html/rfc4511#section-4.1.9");
			} else {
				set_server_check_status(check, HCHK_STATUS_L7OKD, "Success");
			}
		}
		break;

	case PR_O2_SPOP_CHK: {
		unsigned int framesz;
		char	     err[HCHK_DESC_LEN];

		if (!done && check->bi->i < 4)
			goto wait_more_data;

		memcpy(&framesz, check->bi->data, 4);
		framesz = ntohl(framesz);

		if (!done && check->bi->i < (4+framesz))
		    goto wait_more_data;

		if (!handle_spoe_healthcheck_response(check->bi->data+4, framesz, err, HCHK_DESC_LEN-1))
			set_server_check_status(check, HCHK_STATUS_L7OKD, "SPOA server is ok");
		else
			set_server_check_status(check, HCHK_STATUS_L7STS, err);
		break;
	}

	default:
		/* for other checks (eg: pure TCP), delegate to the main task */
		break;
	} /* switch */

 out_wakeup:
	/* collect possible new errors */
	if (conn->flags & CO_FL_ERROR)
		chk_report_conn_err(conn, 0, 0);

	/* Reset the check buffer... */
	*check->bi->data = '\0';
	check->bi->i = 0;

	/* Close the connection... We still attempt to nicely close if,
	 * for instance, SSL needs to send a "close notify." Later, we perform
	 * a hard close and reset the connection if some data are pending,
	 * otherwise we end up with many TIME_WAITs and eat all the source port
	 * range quickly.  To avoid sending RSTs all the time, we first try to
	 * drain pending data.
	 */
	__conn_data_stop_both(conn);
	conn_data_shutw(conn);

	/* OK, let's not stay here forever */
	if (check->result == CHK_RES_FAILED)
		conn->flags |= CO_FL_ERROR;

	task_wakeup(t, TASK_WOKEN_IO);
	return;

 wait_more_data:
	__conn_data_want_recv(conn);
}

/*
 * This function is used only for server health-checks. It handles connection
 * status updates including errors. If necessary, it wakes the check task up.
 * It always returns 0.
 */
static int wake_srv_chk(struct connection *conn)
{
	struct check *check = conn->owner;

	if (unlikely(conn->flags & CO_FL_ERROR)) {
		/* We may get error reports bypassing the I/O handlers, typically
		 * the case when sending a pure TCP check which fails, then the I/O
		 * handlers above are not called. This is completely handled by the
		 * main processing task so let's simply wake it up. If we get here,
		 * we expect errno to still be valid.
		 */
		chk_report_conn_err(conn, errno, 0);

		__conn_data_stop_both(conn);
		task_wakeup(check->task, TASK_WOKEN_IO);
	}
	else if (!(conn->flags & (CO_FL_DATA_RD_ENA|CO_FL_DATA_WR_ENA|CO_FL_HANDSHAKE))) {
		/* we may get here if only a connection probe was required : we
		 * don't have any data to send nor anything expected in response,
		 * so the completion of the connection establishment is enough.
		 */
		task_wakeup(check->task, TASK_WOKEN_IO);
	}

	if (check->result != CHK_RES_UNKNOWN) {
		/* We're here because nobody wants to handle the error, so we
		 * sure want to abort the hard way.
		 */
		conn_sock_drain(conn);
		conn_force_close(conn);
	}
	return 0;
}

struct data_cb check_conn_cb = {
	.recv = event_srv_chk_r,
	.send = event_srv_chk_w,
	.wake = wake_srv_chk,
	.name = "CHCK",
};

/*
 * updates the server's weight during a warmup stage. Once the final weight is
 * reached, the task automatically stops. Note that any server status change
 * must have updated s->last_change accordingly.
 */
static struct task *server_warmup(struct task *t)
{
	struct server *s = t->context;

	/* by default, plan on stopping the task */
	t->expire = TICK_ETERNITY;
	if ((s->admin & SRV_ADMF_MAINT) ||
	    (s->state != SRV_ST_STARTING))
		return t;

	/* recalculate the weights and update the state */
	server_recalc_eweight(s);

	/* probably that we can refill this server with a bit more connections */
	pendconn_grab_from_px(s);

	/* get back there in 1 second or 1/20th of the slowstart interval,
	 * whichever is greater, resulting in small 5% steps.
	 */
	if (s->state == SRV_ST_STARTING)
		t->expire = tick_add(now_ms, MS_TO_TICKS(MAX(1000, s->slowstart / 20)));
	return t;
}

/*
 * establish a server health-check that makes use of a connection.
 *
 * It can return one of :
 *  - SF_ERR_NONE if everything's OK and tcpcheck_main() was not called
 *  - SF_ERR_UP if if everything's OK and tcpcheck_main() was called
 *  - SF_ERR_SRVTO if there are no more servers
 *  - SF_ERR_SRVCL if the connection was refused by the server
 *  - SF_ERR_PRXCOND if the connection has been limited by the proxy (maxconn)
 *  - SF_ERR_RESOURCE if a system resource is lacking (eg: fd limits, ports, ...)
 *  - SF_ERR_INTERNAL for any other purely internal errors
 *  - SF_ERR_CHK_PORT if no port could be found to run a health check on an AF_INET* socket
 * Additionally, in the case of SF_ERR_RESOURCE, an emergency log will be emitted.
 * Note that we try to prevent the network stack from sending the ACK during the
 * connect() when a pure TCP check is used (without PROXY protocol).
 */
static int connect_conn_chk(struct task *t)
{
	struct check *check = t->context;
	struct server *s = check->server;
	struct connection *conn = check->conn;
	struct protocol *proto;
	int ret;
	int quickack;

	/* tcpcheck send/expect initialisation */
	if (check->type == PR_O2_TCPCHK_CHK)
		check->current_step = NULL;

	/* prepare the check buffer.
	 * This should not be used if check is the secondary agent check
	 * of a server as s->proxy->check_req will relate to the
	 * configuration of the primary check. Similarly, tcp-check uses
	 * its own strings.
	 */
	if (check->type && check->type != PR_O2_TCPCHK_CHK && !(check->state & CHK_ST_AGENT)) {
		bo_putblk(check->bo, s->proxy->check_req, s->proxy->check_len);

		/* we want to check if this host replies to HTTP or SSLv3 requests
		 * so we'll send the request, and won't wake the checker up now.
		 */
		if ((check->type) == PR_O2_SSL3_CHK) {
			/* SSL requires that we put Unix time in the request */
			int gmt_time = htonl(date.tv_sec);
			memcpy(check->bo->data + 11, &gmt_time, 4);
		}
		else if ((check->type) == PR_O2_HTTP_CHK) {
			if (s->proxy->options2 & PR_O2_CHK_SNDST)
				bo_putblk(check->bo, trash.str, httpchk_build_status_header(s, trash.str, trash.size));
			/* prevent HTTP keep-alive when "http-check expect" is used */
			if (s->proxy->options2 & PR_O2_EXP_TYPE)
				bo_putstr(check->bo, "Connection: close\r\n");
			bo_putstr(check->bo, "\r\n");
			*check->bo->p = '\0'; /* to make gdb output easier to read */
		}
	}

	if ((check->type & PR_O2_LB_AGENT_CHK) && check->send_string_len) {
		bo_putblk(check->bo, check->send_string, check->send_string_len);
	}

	/* prepare a new connection */
	conn_init(conn);

	if (is_addr(&check->addr)) {
		/* we'll connect to the check addr specified on the server */
		conn->addr.to = check->addr;
	}
	else {
		/* we'll connect to the addr on the server */
		conn->addr.to = s->addr;
	}

       if ((conn->addr.to.ss_family == AF_INET) || (conn->addr.to.ss_family == AF_INET6)) {
		int i = 0;

		i = srv_check_healthcheck_port(check);
		if (i == 0) {
			conn->owner = check;
			return SF_ERR_CHK_PORT;
		}

		set_host_port(&conn->addr.to, i);
	}

	proto = protocol_by_family(conn->addr.to.ss_family);

	conn_prepare(conn, proto, check->xprt);
	conn_attach(conn, check, &check_conn_cb);
	conn->target = &s->obj_type;

	/* no client address */
	clear_addr(&conn->addr.from);

	/* only plain tcp-check supports quick ACK */
	quickack = check->type == 0 || check->type == PR_O2_TCPCHK_CHK;

	if (check->type == PR_O2_TCPCHK_CHK && !LIST_ISEMPTY(check->tcpcheck_rules)) {
		struct tcpcheck_rule *r;

		r = LIST_NEXT(check->tcpcheck_rules, struct tcpcheck_rule *, list);

		/* if first step is a 'connect', then tcpcheck_main must run it */
		if (r->action == TCPCHK_ACT_CONNECT) {
			tcpcheck_main(conn);
			return SF_ERR_UP;
		}
		if (r->action == TCPCHK_ACT_EXPECT)
			quickack = 0;
	}

	ret = SF_ERR_INTERNAL;
	if (proto->connect)
		ret = proto->connect(conn, check->type, quickack ? 2 : 0);
	conn->flags |= CO_FL_WAKE_DATA;
	if (s->check.send_proxy && !(check->state & CHK_ST_AGENT)) {
		conn->send_proxy_ofs = 1;
		conn->flags |= CO_FL_SEND_PROXY;
	}

	return ret;
}

static struct list pid_list = LIST_HEAD_INIT(pid_list);
static struct pool_head *pool2_pid_list;

void block_sigchld(void)
{
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGCHLD);
	assert(sigprocmask(SIG_BLOCK, &set, NULL) == 0);
}

void unblock_sigchld(void)
{
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGCHLD);
	assert(sigprocmask(SIG_UNBLOCK, &set, NULL) == 0);
}

static struct pid_list *pid_list_add(pid_t pid, struct task *t)
{
	struct pid_list *elem;
	struct check *check = t->context;

	elem = pool_alloc2(pool2_pid_list);
	if (!elem)
		return NULL;
	elem->pid = pid;
	elem->t = t;
	elem->exited = 0;
	check->curpid = elem;
	LIST_INIT(&elem->list);
	LIST_ADD(&pid_list, &elem->list);
	return elem;
}

static void pid_list_del(struct pid_list *elem)
{
	struct check *check;

	if (!elem)
		return;

	LIST_DEL(&elem->list);
	if (!elem->exited)
		kill(elem->pid, SIGTERM);

	check = elem->t->context;
	check->curpid = NULL;
	pool_free2(pool2_pid_list, elem);
}

/* Called from inside SIGCHLD handler, SIGCHLD is blocked */
static void pid_list_expire(pid_t pid, int status)
{
	struct pid_list *elem;

	list_for_each_entry(elem, &pid_list, list) {
		if (elem->pid == pid) {
			elem->t->expire = now_ms;
			elem->status = status;
			elem->exited = 1;
			task_wakeup(elem->t, TASK_WOKEN_IO);
			return;
		}
	}
}

static void sigchld_handler(struct sig_handler *sh)
{
	pid_t pid;
	int status;

	while ((pid = waitpid(0, &status, WNOHANG)) > 0)
		pid_list_expire(pid, status);
}

static int init_pid_list(void)
{
	if (pool2_pid_list != NULL)
		/* Nothing to do */
		return 0;

	if (!signal_register_fct(SIGCHLD, sigchld_handler, SIGCHLD)) {
		Alert("Failed to set signal handler for external health checks: %s. Aborting.\n",
		      strerror(errno));
		return 1;
	}

	pool2_pid_list = create_pool("pid_list", sizeof(struct pid_list), MEM_F_SHARED);
	if (pool2_pid_list == NULL) {
		Alert("Failed to allocate memory pool for external health checks: %s. Aborting.\n",
		      strerror(errno));
		return 1;
	}

	return 0;
}

/* helper macro to set an environment variable and jump to a specific label on failure. */
#define EXTCHK_SETENV(check, envidx, value, fail) { if (extchk_setenv(check, envidx, value)) goto fail; }

/*
 * helper function to allocate enough memory to store an environment variable.
 * It will also check that the environment variable is updatable, and silently
 * fail if not.
 */
static int extchk_setenv(struct check *check, int idx, const char *value)
{
	int len, ret;
	char *envname;
	int vmaxlen;

	if (idx < 0 || idx >= EXTCHK_SIZE) {
		Alert("Illegal environment variable index %d. Aborting.\n", idx);
		return 1;
	}

	envname = extcheck_envs[idx].name;
	vmaxlen = extcheck_envs[idx].vmaxlen;

	/* Check if the environment variable is already set, and silently reject
	 * the update if this one is not updatable. */
	if ((vmaxlen == EXTCHK_SIZE_EVAL_INIT) && (check->envp[idx]))
		return 0;

	/* Instead of sending NOT_USED, sending an empty value is preferable */
	if (strcmp(value, "NOT_USED") == 0) {
		value = "";
	}

	len = strlen(envname) + 1;
	if (vmaxlen == EXTCHK_SIZE_EVAL_INIT)
		len += strlen(value);
	else
		len += vmaxlen;

	if (!check->envp[idx])
		check->envp[idx] = malloc(len + 1);

	if (!check->envp[idx]) {
		Alert("Failed to allocate memory for the environment variable '%s'. Aborting.\n", envname);
		return 1;
	}
	ret = snprintf(check->envp[idx], len + 1, "%s=%s", envname, value);
	if (ret < 0) {
		Alert("Failed to store the environment variable '%s'. Reason : %s. Aborting.\n", envname, strerror(errno));
		return 1;
	}
	else if (ret > len) {
		Alert("Environment variable '%s' was truncated. Aborting.\n", envname);
		return 1;
	}
	return 0;
}

static int prepare_external_check(struct check *check)
{
	struct server *s = check->server;
	struct proxy *px = s->proxy;
	struct listener *listener = NULL, *l;
	int i;
	const char *path = px->check_path ? px->check_path : DEF_CHECK_PATH;
	char buf[256];

	list_for_each_entry(l, &px->conf.listeners, by_fe)
		/* Use the first INET, INET6 or UNIX listener */
		if (l->addr.ss_family == AF_INET ||
		    l->addr.ss_family == AF_INET6 ||
		    l->addr.ss_family == AF_UNIX) {
			listener = l;
			break;
		}

	check->curpid = NULL;
	check->envp = calloc((EXTCHK_SIZE + 1), sizeof(char *));
	if (!check->envp) {
		Alert("Failed to allocate memory for environment variables. Aborting\n");
		goto err;
	}

	check->argv = calloc(6, sizeof(char *));
	if (!check->argv) {
		Alert("Starting [%s:%s] check: out of memory.\n", px->id, s->id);
		goto err;
	}

	check->argv[0] = px->check_command;

	if (!listener) {
		check->argv[1] = strdup("NOT_USED");
		check->argv[2] = strdup("NOT_USED");
	}
	else if (listener->addr.ss_family == AF_INET ||
	    listener->addr.ss_family == AF_INET6) {
		addr_to_str(&listener->addr, buf, sizeof(buf));
		check->argv[1] = strdup(buf);
		port_to_str(&listener->addr, buf, sizeof(buf));
		check->argv[2] = strdup(buf);
	}
	else if (listener->addr.ss_family == AF_UNIX) {
		const struct sockaddr_un *un;

		un = (struct sockaddr_un *)&listener->addr;
		check->argv[1] = strdup(un->sun_path);
		check->argv[2] = strdup("NOT_USED");
	}
	else {
		Alert("Starting [%s:%s] check: unsupported address family.\n", px->id, s->id);
		goto err;
	}

	addr_to_str(&s->addr, buf, sizeof(buf));
	check->argv[3] = strdup(buf);

	if (s->addr.ss_family == AF_INET || s->addr.ss_family == AF_INET6)
		snprintf(buf, sizeof(buf), "%u", s->svc_port);
	else
		*buf = 0;
	check->argv[4] = strdup(buf);

	for (i = 0; i < 5; i++) {
		if (!check->argv[i]) {
			Alert("Starting [%s:%s] check: out of memory.\n", px->id, s->id);
			goto err;
		}
	}

	EXTCHK_SETENV(check, EXTCHK_PATH, path, err);
	/* Add proxy environment variables */
	EXTCHK_SETENV(check, EXTCHK_HAPROXY_PROXY_NAME, px->id, err);
	EXTCHK_SETENV(check, EXTCHK_HAPROXY_PROXY_ID, ultoa_r(px->uuid, buf, sizeof(buf)), err);
	EXTCHK_SETENV(check, EXTCHK_HAPROXY_PROXY_ADDR, check->argv[1], err);
	EXTCHK_SETENV(check, EXTCHK_HAPROXY_PROXY_PORT, check->argv[2], err);
	/* Add server environment variables */
	EXTCHK_SETENV(check, EXTCHK_HAPROXY_SERVER_NAME, s->id, err);
	EXTCHK_SETENV(check, EXTCHK_HAPROXY_SERVER_ID, ultoa_r(s->puid, buf, sizeof(buf)), err);
	EXTCHK_SETENV(check, EXTCHK_HAPROXY_SERVER_ADDR, check->argv[3], err);
	EXTCHK_SETENV(check, EXTCHK_HAPROXY_SERVER_PORT, check->argv[4], err);
	EXTCHK_SETENV(check, EXTCHK_HAPROXY_SERVER_MAXCONN, ultoa_r(s->maxconn, buf, sizeof(buf)), err);
	EXTCHK_SETENV(check, EXTCHK_HAPROXY_SERVER_CURCONN, ultoa_r(s->cur_sess, buf, sizeof(buf)), err);

	/* Ensure that we don't leave any hole in check->envp */
	for (i = 0; i < EXTCHK_SIZE; i++)
		if (!check->envp[i])
			EXTCHK_SETENV(check, i, "", err);

	return 1;
err:
	if (check->envp) {
		for (i = 0; i < EXTCHK_SIZE; i++)
			free(check->envp[i]);
		free(check->envp);
		check->envp = NULL;
	}

	if (check->argv) {
		for (i = 1; i < 5; i++)
			free(check->argv[i]);
		free(check->argv);
		check->argv = NULL;
	}
	return 0;
}

/*
 * establish a server health-check that makes use of a process.
 *
 * It can return one of :
 *  - SF_ERR_NONE if everything's OK
 *  - SF_ERR_SRVTO if there are no more servers
 *  - SF_ERR_SRVCL if the connection was refused by the server
 *  - SF_ERR_PRXCOND if the connection has been limited by the proxy (maxconn)
 *  - SF_ERR_RESOURCE if a system resource is lacking (eg: fd limits, ports, ...)
 *  - SF_ERR_INTERNAL for any other purely internal errors
 * Additionally, in the case of SF_ERR_RESOURCE, an emergency log will be emitted.
 *
 * Blocks and then unblocks SIGCHLD
 */
static int connect_proc_chk(struct task *t)
{
	char buf[256];
	struct check *check = t->context;
	struct server *s = check->server;
	struct proxy *px = s->proxy;
	int status;
	pid_t pid;

	status = SF_ERR_RESOURCE;

	block_sigchld();

#ifdef __VMS		// >>> BRC; more work required here!
	pid = vfork();
#else
	pid = fork();
#endif
	if (pid < 0) {
		Alert("Failed to fork process for external health check: %s. Aborting.\n",
		      strerror(errno));
		set_server_check_status(check, HCHK_STATUS_SOCKERR, strerror(errno));
		goto out;
	}
	if (pid == 0) {
		/* Child */
		extern char **environ;
		int fd;

		/* close all FDs. Keep stdin/stdout/stderr in verbose mode */
		fd = (global.mode & (MODE_QUIET|MODE_VERBOSE)) == MODE_QUIET ? 0 : 3;

		while (fd < global.rlimit_nofile)
			close(fd++);

		environ = check->envp;
		extchk_setenv(check, EXTCHK_HAPROXY_SERVER_CURCONN, ultoa_r(s->cur_sess, buf, sizeof(buf)));
		execvp(px->check_command, check->argv);
		Alert("Failed to exec process for external health check: %s. Aborting.\n",
		      strerror(errno));
		exit(-1);
	}

	/* Parent */
	if (check->result == CHK_RES_UNKNOWN) {
		if (pid_list_add(pid, t) != NULL) {
			t->expire = tick_add(now_ms, MS_TO_TICKS(check->inter));

			if (px->timeout.check && px->timeout.connect) {
				int t_con = tick_add(now_ms, px->timeout.connect);
				t->expire = tick_first(t->expire, t_con);
			}
			status = SF_ERR_NONE;
			goto out;
		}
		else {
			set_server_check_status(check, HCHK_STATUS_SOCKERR, strerror(errno));
		}
		kill(pid, SIGTERM); /* process creation error */
	}
	else
		set_server_check_status(check, HCHK_STATUS_SOCKERR, strerror(errno));

out:
	unblock_sigchld();
	return status;
}

/*
 * manages a server health-check that uses a process. Returns
 * the time the task accepts to wait, or TIME_ETERNITY for infinity.
 */
static struct task *process_chk_proc(struct task *t)
{
	struct check *check = t->context;
	struct server *s = check->server;
	struct connection *conn = check->conn;
	int rv;
	int ret;
	int expired = tick_is_expired(t->expire, now_ms);

	if (!(check->state & CHK_ST_INPROGRESS)) {
		/* no check currently running */
		if (!expired) /* woke up too early */
			return t;

		/* we don't send any health-checks when the proxy is
		 * stopped, the server should not be checked or the check
		 * is disabled.
		 */
		if (((check->state & (CHK_ST_ENABLED | CHK_ST_PAUSED)) != CHK_ST_ENABLED) ||
		    s->proxy->state == PR_STSTOPPED)
			goto reschedule;

		/* we'll initiate a new check */
		set_server_check_status(check, HCHK_STATUS_START, NULL);

		check->state |= CHK_ST_INPROGRESS;

		ret = connect_proc_chk(t);
		switch (ret) {
		case SF_ERR_UP:
			return t;
		case SF_ERR_NONE:
			/* we allow up to min(inter, timeout.connect) for a connection
			 * to establish but only when timeout.check is set
			 * as it may be to short for a full check otherwise
			 */
			t->expire = tick_add(now_ms, MS_TO_TICKS(check->inter));

			if (s->proxy->timeout.check && s->proxy->timeout.connect) {
				int t_con = tick_add(now_ms, s->proxy->timeout.connect);
				t->expire = tick_first(t->expire, t_con);
			}

			goto reschedule;

		case SF_ERR_SRVTO: /* ETIMEDOUT */
		case SF_ERR_SRVCL: /* ECONNREFUSED, ENETUNREACH, ... */
			conn->flags |= CO_FL_ERROR;
			chk_report_conn_err(conn, errno, 0);
			break;
		case SF_ERR_PRXCOND:
		case SF_ERR_RESOURCE:
		case SF_ERR_INTERNAL:
			conn->flags |= CO_FL_ERROR;
			chk_report_conn_err(conn, 0, 0);
			break;
		}

		/* here, we have seen a synchronous error, no fd was allocated */

		check->state &= ~CHK_ST_INPROGRESS;
		check_notify_failure(check);

		/* we allow up to min(inter, timeout.connect) for a connection
		 * to establish but only when timeout.check is set
		 * as it may be to short for a full check otherwise
		 */
		while (tick_is_expired(t->expire, now_ms)) {
			int t_con;

			t_con = tick_add(t->expire, s->proxy->timeout.connect);
			t->expire = tick_add(t->expire, MS_TO_TICKS(check->inter));

			if (s->proxy->timeout.check)
				t->expire = tick_first(t->expire, t_con);
		}
	}
	else {
		/* there was a test running.
		 * First, let's check whether there was an uncaught error,
		 * which can happen on connect timeout or error.
		 */
		if (check->result == CHK_RES_UNKNOWN) {
			/* good connection is enough for pure TCP check */
			struct pid_list *elem = check->curpid;
			int status = HCHK_STATUS_UNKNOWN;

			if (elem->exited) {
				status = elem->status; /* Save in case the process exits between use below */
				if (!WIFEXITED(status))
					check->code = -1;
				else
					check->code = WEXITSTATUS(status);
				if (!WIFEXITED(status) || WEXITSTATUS(status))
					status = HCHK_STATUS_PROCERR;
				else
					status = HCHK_STATUS_PROCOK;
			} else if (expired) {
				status = HCHK_STATUS_PROCTOUT;
				Warning("kill %d\n", (int)elem->pid);
				kill(elem->pid, SIGTERM);
			}
			set_server_check_status(check, status, NULL);
		}

		if (check->result == CHK_RES_FAILED) {
			/* a failure or timeout detected */
			check_notify_failure(check);
		}
		else if (check->result == CHK_RES_CONDPASS) {
			/* check is OK but asks for stopping mode */
			check_notify_stopping(check);
		}
		else if (check->result == CHK_RES_PASSED) {
			/* a success was detected */
			check_notify_success(check);
		}
		check->state &= ~CHK_ST_INPROGRESS;

		pid_list_del(check->curpid);

		rv = 0;
		if (global.spread_checks > 0) {
			rv = srv_getinter(check) * global.spread_checks / 100;
			rv -= (int) (2 * rv * (rand() / (RAND_MAX + 1.0)));
		}
		t->expire = tick_add(now_ms, MS_TO_TICKS(srv_getinter(check) + rv));
	}

 reschedule:
	while (tick_is_expired(t->expire, now_ms))
		t->expire = tick_add(t->expire, MS_TO_TICKS(check->inter));
	return t;
}

/*
 * manages a server health-check that uses a connection. Returns
 * the time the task accepts to wait, or TIME_ETERNITY for infinity.
 */
static struct task *process_chk_conn(struct task *t)
{
	struct check *check = t->context;
	struct server *s = check->server;
	struct connection *conn = check->conn;
	int rv;
	int ret;
	int expired = tick_is_expired(t->expire, now_ms);

	if (!(check->state & CHK_ST_INPROGRESS)) {
		/* no check currently running */
		if (!expired) /* woke up too early */
			return t;

		/* we don't send any health-checks when the proxy is
		 * stopped, the server should not be checked or the check
		 * is disabled.
		 */
		if (((check->state & (CHK_ST_ENABLED | CHK_ST_PAUSED)) != CHK_ST_ENABLED) ||
		    s->proxy->state == PR_STSTOPPED)
			goto reschedule;

		/* we'll initiate a new check */
		set_server_check_status(check, HCHK_STATUS_START, NULL);

		check->state |= CHK_ST_INPROGRESS;
		check->bi->p = check->bi->data;
		check->bi->i = 0;
		check->bo->p = check->bo->data;
		check->bo->o = 0;

		ret = connect_conn_chk(t);
		switch (ret) {
		case SF_ERR_UP:
			return t;
		case SF_ERR_NONE:
			/* we allow up to min(inter, timeout.connect) for a connection
			 * to establish but only when timeout.check is set
			 * as it may be to short for a full check otherwise
			 */
			t->expire = tick_add(now_ms, MS_TO_TICKS(check->inter));

			if (s->proxy->timeout.check && s->proxy->timeout.connect) {
				int t_con = tick_add(now_ms, s->proxy->timeout.connect);
				t->expire = tick_first(t->expire, t_con);
			}

			if (check->type)
				conn_data_want_recv(conn);   /* prepare for reading a possible reply */

			goto reschedule;

		case SF_ERR_SRVTO: /* ETIMEDOUT */
		case SF_ERR_SRVCL: /* ECONNREFUSED, ENETUNREACH, ... */
			conn->flags |= CO_FL_ERROR;
			chk_report_conn_err(conn, errno, 0);
			break;
		/* should share same code than cases below */
		case SF_ERR_CHK_PORT:
			check->state |= CHK_ST_PORT_MISS;
		case SF_ERR_PRXCOND:
		case SF_ERR_RESOURCE:
		case SF_ERR_INTERNAL:
			conn->flags |= CO_FL_ERROR;
			chk_report_conn_err(conn, 0, 0);
			break;
		}

		/* here, we have seen a synchronous error, no fd was allocated */

		check->state &= ~CHK_ST_INPROGRESS;
		check_notify_failure(check);

		/* we allow up to min(inter, timeout.connect) for a connection
		 * to establish but only when timeout.check is set
		 * as it may be to short for a full check otherwise
		 */
		while (tick_is_expired(t->expire, now_ms)) {
			int t_con;

			t_con = tick_add(t->expire, s->proxy->timeout.connect);
			t->expire = tick_add(t->expire, MS_TO_TICKS(check->inter));

			if (s->proxy->timeout.check)
				t->expire = tick_first(t->expire, t_con);
		}
	}
	else {
		/* there was a test running.
		 * First, let's check whether there was an uncaught error,
		 * which can happen on connect timeout or error.
		 */
		if (check->result == CHK_RES_UNKNOWN) {
			/* good connection is enough for pure TCP check */
			if ((conn->flags & CO_FL_CONNECTED) && !check->type) {
				if (check->use_ssl)
					set_server_check_status(check, HCHK_STATUS_L6OK, NULL);
				else
					set_server_check_status(check, HCHK_STATUS_L4OK, NULL);
			}
			else if ((conn->flags & CO_FL_ERROR) || expired) {
				chk_report_conn_err(conn, 0, expired);
			}
			else
				goto out_wait; /* timeout not reached, wait again */
		}

		/* check complete or aborted */
		if (conn->xprt) {
			/* The check was aborted and the connection was not yet closed.
			 * This can happen upon timeout, or when an external event such
			 * as a failed response coupled with "observe layer7" caused the
			 * server state to be suddenly changed.
			 */
			conn_sock_drain(conn);
			conn_force_close(conn);
		}

		if (check->result == CHK_RES_FAILED) {
			/* a failure or timeout detected */
			check_notify_failure(check);
		}
		else if (check->result == CHK_RES_CONDPASS) {
			/* check is OK but asks for stopping mode */
			check_notify_stopping(check);
		}
		else if (check->result == CHK_RES_PASSED) {
			/* a success was detected */
			check_notify_success(check);
		}
		check->state &= ~CHK_ST_INPROGRESS;

		rv = 0;
		if (global.spread_checks > 0) {
			rv = srv_getinter(check) * global.spread_checks / 100;
			rv -= (int) (2 * rv * (rand() / (RAND_MAX + 1.0)));
		}
		t->expire = tick_add(now_ms, MS_TO_TICKS(srv_getinter(check) + rv));
	}

 reschedule:
	while (tick_is_expired(t->expire, now_ms))
		t->expire = tick_add(t->expire, MS_TO_TICKS(check->inter));
 out_wait:
	return t;
}

/*
 * manages a server health-check. Returns
 * the time the task accepts to wait, or TIME_ETERNITY for infinity.
 */
static struct task *process_chk(struct task *t)
{
	struct check *check = t->context;
	struct server *s = check->server;
	struct dns_resolution *resolution = s->resolution;

	/* trigger name resolution */
	if ((s->check.state & CHK_ST_ENABLED) && (resolution)) {
		/* check if a no resolution is running for this server */
		if (resolution->step == RSLV_STEP_NONE) {
			/*
			 * if there has not been any name resolution for a longer period than
			 * hold.valid, let's trigger a new one.
			 */
			if (!resolution->last_resolution || tick_is_expired(tick_add(resolution->last_resolution, resolution->resolvers->hold.valid), now_ms)) {
				trigger_resolution(s);
			}
		}
	}

	if (check->type == PR_O2_EXT_CHK)
		return process_chk_proc(t);
	return process_chk_conn(t);

}

/*
 * Initiates a new name resolution:
 *  - generates a query id
 *  - configure the resolution structure
 *  - startup the resolvers task if required
 *
 * returns:
 *  - 0 in case of error or if resolution already running
 *  - 1 if everything started properly
 */
int trigger_resolution(struct server *s)
{
	struct dns_resolution *resolution;
	struct dns_resolvers *resolvers;
	int query_id;
	int i;

	resolution = s->resolution;
	resolvers = resolution->resolvers;

	/*
	 * check if a resolution has already been started for this server
	 * return directly to avoid resolution pill up
	 */
	if (resolution->step != RSLV_STEP_NONE)
		return 0;

	/* generates a query id */
	i = 0;
	do {
		query_id = dns_rnd16();
		/* we do try only 100 times to find a free query id */
		if (i++ > 100) {
			chunk_printf(&trash, "could not generate a query id for %s/%s, in resolvers %s",
						s->proxy->id, s->id, resolvers->id);

			send_log(s->proxy, LOG_NOTICE, "%s.\n", trash.str);
			return 0;
		}
	} while (eb32_lookup(&resolvers->query_ids, query_id));

	LIST_ADDQ(&resolvers->curr_resolution, &resolution->list);

	/* now update resolution parameters */
	resolution->query_id = query_id;
	resolution->qid.key = query_id;
	resolution->step = RSLV_STEP_RUNNING;
	resolution->opts = &s->dns_opts;
	if (resolution->opts->family_prio == AF_INET) {
		resolution->query_type = DNS_RTYPE_A;
	} else {
		resolution->query_type = DNS_RTYPE_AAAA;
	}
	resolution->try = resolvers->resolve_retries;
	resolution->try_cname = 0;
	resolution->nb_responses = 0;
	eb32_insert(&resolvers->query_ids, &resolution->qid);

	dns_send_query(resolution);
	resolution->try -= 1;

	/* update wakeup date if this resolution is the only one in the FIFO list */
	if (dns_check_resolution_queue(resolvers) == 1) {
		/* update task timeout */
		dns_update_resolvers_timeout(resolvers);
		task_queue(resolvers->t);
	}

	return 1;
}

static int start_check_task(struct check *check, int mininter,
			    int nbcheck, int srvpos)
{
	struct task *t;
	/* task for the check */
	if ((t = task_new()) == NULL) {
		Alert("Starting [%s:%s] check: out of memory.\n",
		      check->server->proxy->id, check->server->id);
		return 0;
	}

	check->task = t;
	t->process = process_chk;
	t->context = check;

	if (mininter < srv_getinter(check))
		mininter = srv_getinter(check);

	if (global.max_spread_checks && mininter > global.max_spread_checks)
		mininter = global.max_spread_checks;

	/* check this every ms */
	t->expire = tick_add(now_ms, MS_TO_TICKS(mininter * srvpos / nbcheck));
	check->start = now;
	task_queue(t);

	return 1;
}

/*
 * Start health-check.
 * Returns 0 if OK, -1 if error, and prints the error in this case.
 */
int start_checks() {

	struct proxy *px;
	struct server *s;
	struct task *t;
	int nbcheck=0, mininter=0, srvpos=0;

	/* 1- count the checkers to run simultaneously.
	 * We also determine the minimum interval among all of those which
	 * have an interval larger than SRV_CHK_INTER_THRES. This interval
	 * will be used to spread their start-up date. Those which have
	 * a shorter interval will start independently and will not dictate
	 * too short an interval for all others.
	 */
	for (px = proxy; px; px = px->next) {
		for (s = px->srv; s; s = s->next) {
			if (s->slowstart) {
				if ((t = task_new()) == NULL) {
					Alert("Starting [%s:%s] check: out of memory.\n", px->id, s->id);
					return -1;
				}
				/* We need a warmup task that will be called when the server
				 * state switches from down to up.
				 */
				s->warmup = t;
				t->process = server_warmup;
				t->context = s;
				t->expire = TICK_ETERNITY;
				/* server can be in this state only because of */
				if (s->state == SRV_ST_STARTING)
					task_schedule(s->warmup, tick_add(now_ms, MS_TO_TICKS(MAX(1000, (now.tv_sec - s->last_change)) / 20)));
			}

			if (s->check.state & CHK_ST_CONFIGURED) {
				nbcheck++;
				if ((srv_getinter(&s->check) >= SRV_CHK_INTER_THRES) &&
				    (!mininter || mininter > srv_getinter(&s->check)))
					mininter = srv_getinter(&s->check);
			}

			if (s->agent.state & CHK_ST_CONFIGURED) {
				nbcheck++;
				if ((srv_getinter(&s->agent) >= SRV_CHK_INTER_THRES) &&
				    (!mininter || mininter > srv_getinter(&s->agent)))
					mininter = srv_getinter(&s->agent);
			}
		}
	}

	if (!nbcheck)
		return 0;

	srand((unsigned)time(NULL));

	/*
	 * 2- start them as far as possible from each others. For this, we will
	 * start them after their interval set to the min interval divided by
	 * the number of servers, weighted by the server's position in the list.
	 */
	for (px = proxy; px; px = px->next) {
		if ((px->options2 & PR_O2_CHK_ANY) == PR_O2_EXT_CHK) {
			if (init_pid_list()) {
				Alert("Starting [%s] check: out of memory.\n", px->id);
				return -1;
			}
		}

		for (s = px->srv; s; s = s->next) {
			/* A task for the main check */
			if (s->check.state & CHK_ST_CONFIGURED) {
				if (s->check.type == PR_O2_EXT_CHK) {
					if (!prepare_external_check(&s->check))
						return -1;
				}
				if (!start_check_task(&s->check, mininter, nbcheck, srvpos))
					return -1;
				srvpos++;
			}

			/* A task for a auxiliary agent check */
			if (s->agent.state & CHK_ST_CONFIGURED) {
				if (!start_check_task(&s->agent, mininter, nbcheck, srvpos)) {
					return -1;
				}
				srvpos++;
			}
		}
	}
	return 0;
}

/*
 * Perform content verification check on data in s->check.buffer buffer.
 * The buffer MUST be terminated by a null byte before calling this function.
 * Sets server status appropriately. The caller is responsible for ensuring
 * that the buffer contains at least 13 characters. If <done> is zero, we may
 * return 0 to indicate that data is required to decide of a match.
 */
static int httpchk_expect(struct server *s, int done)
{
	static char status_msg[] = "HTTP status check returned code <000>";
	char status_code[] = "000";
	char *contentptr;
	int crlf;
	int ret;

	switch (s->proxy->options2 & PR_O2_EXP_TYPE) {
	case PR_O2_EXP_STS:
	case PR_O2_EXP_RSTS:
		memcpy(status_code, s->check.bi->data + 9, 3);
		memcpy(status_msg + strlen(status_msg) - 4, s->check.bi->data + 9, 3);

		if ((s->proxy->options2 & PR_O2_EXP_TYPE) == PR_O2_EXP_STS)
			ret = strncmp(s->proxy->expect_str, status_code, 3) == 0;
		else
			ret = regex_exec(s->proxy->expect_regex, status_code);

		/* we necessarily have the response, so there are no partial failures */
		if (s->proxy->options2 & PR_O2_EXP_INV)
			ret = !ret;

		set_server_check_status(&s->check, ret ? HCHK_STATUS_L7OKD : HCHK_STATUS_L7STS, status_msg);
		break;

	case PR_O2_EXP_STR:
	case PR_O2_EXP_RSTR:
		/* very simple response parser: ignore CR and only count consecutive LFs,
		 * stop with contentptr pointing to first char after the double CRLF or
		 * to '\0' if crlf < 2.
		 */
		crlf = 0;
		for (contentptr = s->check.bi->data; *contentptr; contentptr++) {
			if (crlf >= 2)
				break;
			if (*contentptr == '\r')
				continue;
			else if (*contentptr == '\n')
				crlf++;
			else
				crlf = 0;
		}

		/* Check that response contains a body... */
		if (crlf < 2) {
			if (!done)
				return 0;

			set_server_check_status(&s->check, HCHK_STATUS_L7RSP,
						"HTTP content check could not find a response body");
			return 1;
		}

		/* Check that response body is not empty... */
		if (*contentptr == '\0') {
			if (!done)
				return 0;

			set_server_check_status(&s->check, HCHK_STATUS_L7RSP,
						"HTTP content check found empty response body");
			return 1;
		}

		/* Check the response content against the supplied string
		 * or regex... */
		if ((s->proxy->options2 & PR_O2_EXP_TYPE) == PR_O2_EXP_STR)
			ret = strstr(contentptr, s->proxy->expect_str) != NULL;
		else
			ret = regex_exec(s->proxy->expect_regex, contentptr);

		/* if we don't match, we may need to wait more */
		if (!ret && !done)
			return 0;

		if (ret) {
			/* content matched */
			if (s->proxy->options2 & PR_O2_EXP_INV)
				set_server_check_status(&s->check, HCHK_STATUS_L7RSP,
							"HTTP check matched unwanted content");
			else
				set_server_check_status(&s->check, HCHK_STATUS_L7OKD,
							"HTTP content check matched");
		}
		else {
			if (s->proxy->options2 & PR_O2_EXP_INV)
				set_server_check_status(&s->check, HCHK_STATUS_L7OKD,
							"HTTP check did not match unwanted content");
			else
				set_server_check_status(&s->check, HCHK_STATUS_L7RSP,
							"HTTP content check did not match");
		}
		break;
	}
	return 1;
}

/*
 * return the id of a step in a send/expect session
 */
static int tcpcheck_get_step_id(struct check *check)
{
	struct tcpcheck_rule *cur = NULL, *next = NULL;
	int i = 0;

	/* not even started anything yet => step 0 = initial connect */
	if (!check->current_step)
		return 0;

	cur = check->last_started_step;

	/* no step => first step */
	if (cur == NULL)
		return 1;

	/* increment i until current step */
	list_for_each_entry(next, check->tcpcheck_rules, list) {
		if (next->list.p == &cur->list)
			break;
		++i;
	}

	return i;
}

/*
 * return the latest known comment before (including) the given stepid
 * returns NULL if no comment found
 */
static char * tcpcheck_get_step_comment(struct check *check, int stepid)
{
	struct tcpcheck_rule *cur = NULL;
	char *ret = NULL;
	int i = 0;

	/* not even started anything yet, return latest comment found before any action */
	if (!check->current_step) {
		list_for_each_entry(cur, check->tcpcheck_rules, list) {
			if (cur->action == TCPCHK_ACT_COMMENT)
				ret = cur->comment;
			else
				goto return_comment;
		}
	}

	i = 1;
	list_for_each_entry(cur, check->tcpcheck_rules, list) {
		if (cur->comment)
			ret = cur->comment;

		if (i >= stepid)
			goto return_comment;

		++i;
	}

 return_comment:
	return ret;
}

static void tcpcheck_main(struct connection *conn)
{
	char *contentptr, *comment;
	struct tcpcheck_rule *next;
	int done = 0, ret = 0, step = 0;
	struct check *check = conn->owner;
	struct server *s = check->server;
	struct task *t = check->task;
	struct list *head = check->tcpcheck_rules;

	/* here, we know that the check is complete or that it failed */
	if (check->result != CHK_RES_UNKNOWN)
		goto out_end_tcpcheck;

	/* We have 4 possibilities here :
	 *   1. we've not yet attempted step 1, and step 1 is a connect, so no
	 *      connection attempt was made yet ;
	 *   2. we've not yet attempted step 1, and step 1 is a not connect or
	 *      does not exist (no rule), so a connection attempt was made
	 *      before coming here.
	 *   3. we're coming back after having started with step 1, so we may
	 *      be waiting for a connection attempt to complete.
	 *   4. the connection + handshake are complete
	 *
	 * #2 and #3 are quite similar, we want both the connection and the
	 * handshake to complete before going any further. Thus we must always
	 * wait for a connection to complete unless we're before and existing
	 * step 1.
	 */

	/* find first rule and skip comments */
	next = LIST_NEXT(head, struct tcpcheck_rule *, list);
	while (&next->list != head && next->action == TCPCHK_ACT_COMMENT)
		next = LIST_NEXT(&next->list, struct tcpcheck_rule *, list);

	if ((!(conn->flags & CO_FL_CONNECTED) || (conn->flags & CO_FL_HANDSHAKE)) &&
	    (check->current_step || &next->list == head)) {
		/* we allow up to min(inter, timeout.connect) for a connection
		 * to establish but only when timeout.check is set
		 * as it may be to short for a full check otherwise
		 */
		while (tick_is_expired(t->expire, now_ms)) {
			int t_con;

			t_con = tick_add(t->expire, s->proxy->timeout.connect);
			t->expire = tick_add(t->expire, MS_TO_TICKS(check->inter));

			if (s->proxy->timeout.check)
				t->expire = tick_first(t->expire, t_con);
		}
		return;
	}

	/* special case: option tcp-check with no rule, a connect is enough */
	if (&next->list == head) {
		set_server_check_status(check, HCHK_STATUS_L4OK, NULL);
		goto out_end_tcpcheck;
	}

	/* no step means first step initialisation */
	if (check->current_step == NULL) {
		check->last_started_step = NULL;
		check->bo->p = check->bo->data;
		check->bo->o = 0;
		check->bi->p = check->bi->data;
		check->bi->i = 0;
		check->current_step = next;
		t->expire = tick_add(now_ms, MS_TO_TICKS(check->inter));
		if (s->proxy->timeout.check)
			t->expire = tick_add_ifset(now_ms, s->proxy->timeout.check);
	}

	/* It's only the rules which will enable send/recv */
	__conn_data_stop_both(conn);

	while (1) {
		/* We have to try to flush the output buffer before reading, at
		 * the end, or if we're about to send a string that does not fit
		 * in the remaining space. That explains why we break out of the
		 * loop after this control.
		 */
		if (check->bo->o &&
		    (&check->current_step->list == head ||
		     check->current_step->action != TCPCHK_ACT_SEND ||
		     check->current_step->string_len >= buffer_total_space(check->bo))) {

			if (conn->xprt->snd_buf(conn, check->bo, 0) <= 0) {
				if (conn->flags & CO_FL_ERROR) {
					chk_report_conn_err(conn, errno, 0);
					__conn_data_stop_both(conn);
					goto out_end_tcpcheck;
				}
				break;
			}
		}

		if (&check->current_step->list == head)
			break;

		/* have 'next' point to the next rule or NULL if we're on the
		 * last one, connect() needs this.
		 */
		next = LIST_NEXT(&check->current_step->list, struct tcpcheck_rule *, list);

		/* bypass all comment rules */
		while (&next->list != head && next->action == TCPCHK_ACT_COMMENT)
			next = LIST_NEXT(&next->list, struct tcpcheck_rule *, list);

		/* NULL if we're on the last rule */
		if (&next->list == head)
			next = NULL;

		if (check->current_step->action == TCPCHK_ACT_CONNECT) {
			struct protocol *proto;
			struct xprt_ops *xprt;

			/* mark the step as started */
			check->last_started_step = check->current_step;
			/* first, shut existing connection */
			conn_force_close(conn);

			/* prepare new connection */
			/* initialization */
			conn_init(conn);
			conn_attach(conn, check, &check_conn_cb);
			conn->target = &s->obj_type;

			/* no client address */
			clear_addr(&conn->addr.from);

			if (is_addr(&check->addr)) {
				/* we'll connect to the check addr specified on the server */
				conn->addr.to = check->addr;
			}
			else {
				/* we'll connect to the addr on the server */
				conn->addr.to = s->addr;
			}
			proto = protocol_by_family(conn->addr.to.ss_family);

			/* port */
			if (check->current_step->port)
				set_host_port(&conn->addr.to, check->current_step->port);
			else if (check->port)
				set_host_port(&conn->addr.to, check->port);

#ifdef USE_OPENSSL
			if (check->current_step->conn_opts & TCPCHK_OPT_SSL) {
				xprt = &ssl_sock;
			}
			else {
				xprt = &raw_sock;
			}
#else  /* USE_OPENSSL */
			xprt = &raw_sock;
#endif /* USE_OPENSSL */
			conn_prepare(conn, proto, xprt);

			ret = SF_ERR_INTERNAL;
			if (proto->connect)
				ret = proto->connect(conn,
						     1 /* I/O polling is always needed */,
						     (next && next->action == TCPCHK_ACT_EXPECT) ? 0 : 2);
			conn->flags |= CO_FL_WAKE_DATA;
			if (check->current_step->conn_opts & TCPCHK_OPT_SEND_PROXY) {
				conn->send_proxy_ofs = 1;
				conn->flags |= CO_FL_SEND_PROXY;
			}

			/* It can return one of :
			 *  - SF_ERR_NONE if everything's OK
			 *  - SF_ERR_SRVTO if there are no more servers
			 *  - SF_ERR_SRVCL if the connection was refused by the server
			 *  - SF_ERR_PRXCOND if the connection has been limited by the proxy (maxconn)
			 *  - SF_ERR_RESOURCE if a system resource is lacking (eg: fd limits, ports, ...)
			 *  - SF_ERR_INTERNAL for any other purely internal errors
			 * Additionally, in the case of SF_ERR_RESOURCE, an emergency log will be emitted.
			 * Note that we try to prevent the network stack from sending the ACK during the
			 * connect() when a pure TCP check is used (without PROXY protocol).
			 */
			switch (ret) {
			case SF_ERR_NONE:
				/* we allow up to min(inter, timeout.connect) for a connection
				 * to establish but only when timeout.check is set
				 * as it may be to short for a full check otherwise
				 */
				t->expire = tick_add(now_ms, MS_TO_TICKS(check->inter));

				if (s->proxy->timeout.check && s->proxy->timeout.connect) {
					int t_con = tick_add(now_ms, s->proxy->timeout.connect);
					t->expire = tick_first(t->expire, t_con);
				}
				break;
			case SF_ERR_SRVTO: /* ETIMEDOUT */
			case SF_ERR_SRVCL: /* ECONNREFUSED, ENETUNREACH, ... */
				step = tcpcheck_get_step_id(check);
				chunk_printf(&trash, "TCPCHK error establishing connection at step %d: %s",
						step, strerror(errno));
				comment = tcpcheck_get_step_comment(check, step);
				if (comment)
					chunk_appendf(&trash, " comment: '%s'", comment);
				set_server_check_status(check, HCHK_STATUS_L4CON, trash.str);
				goto out_end_tcpcheck;
			case SF_ERR_PRXCOND:
			case SF_ERR_RESOURCE:
			case SF_ERR_INTERNAL:
				step = tcpcheck_get_step_id(check);
				chunk_printf(&trash, "TCPCHK error establishing connection at step %d", step);
				comment = tcpcheck_get_step_comment(check, step);
				if (comment)
					chunk_appendf(&trash, " comment: '%s'", comment);
				set_server_check_status(check, HCHK_STATUS_SOCKERR, trash.str);
				goto out_end_tcpcheck;
			}

			/* allow next rule */
			check->current_step = LIST_NEXT(&check->current_step->list, struct tcpcheck_rule *, list);

			/* bypass all comment rules */
			while (&check->current_step->list != head &&
				check->current_step->action == TCPCHK_ACT_COMMENT)
				check->current_step = LIST_NEXT(&check->current_step->list, struct tcpcheck_rule *, list);

			if (&check->current_step->list == head)
				break;

			/* don't do anything until the connection is established */
			if (!(conn->flags & CO_FL_CONNECTED)) {
				/* update expire time, should be done by process_chk */
				/* we allow up to min(inter, timeout.connect) for a connection
				 * to establish but only when timeout.check is set
				 * as it may be to short for a full check otherwise
				 */
				while (tick_is_expired(t->expire, now_ms)) {
					int t_con;

					t_con = tick_add(t->expire, s->proxy->timeout.connect);
					t->expire = tick_add(t->expire, MS_TO_TICKS(check->inter));

					if (s->proxy->timeout.check)
						t->expire = tick_first(t->expire, t_con);
				}
				return;
			}

		} /* end 'connect' */
		else if (check->current_step->action == TCPCHK_ACT_SEND) {
			/* mark the step as started */
			check->last_started_step = check->current_step;

			/* reset the read buffer */
			if (*check->bi->data != '\0') {
				*check->bi->data = '\0';
				check->bi->i = 0;
			}

			if (conn->flags & (CO_FL_SOCK_WR_SH | CO_FL_DATA_WR_SH)) {
				conn->flags |= CO_FL_ERROR;
				chk_report_conn_err(conn, 0, 0);
				goto out_end_tcpcheck;
			}

			if (check->current_step->string_len >= check->bo->size) {
				chunk_printf(&trash, "tcp-check send : string too large (%d) for buffer size (%d) at step %d",
					     check->current_step->string_len, check->bo->size,
					     tcpcheck_get_step_id(check));
				set_server_check_status(check, HCHK_STATUS_L7RSP, trash.str);
				goto out_end_tcpcheck;
			}

			/* do not try to send if there is no space */
			if (check->current_step->string_len >= buffer_total_space(check->bo))
				continue;

			bo_putblk(check->bo, check->current_step->string, check->current_step->string_len);
			*check->bo->p = '\0'; /* to make gdb output easier to read */

			/* go to next rule and try to send */
			check->current_step = LIST_NEXT(&check->current_step->list, struct tcpcheck_rule *, list);

			/* bypass all comment rules */
			while (&check->current_step->list != head &&
				check->current_step->action == TCPCHK_ACT_COMMENT)
				check->current_step = LIST_NEXT(&check->current_step->list, struct tcpcheck_rule *, list);

			if (&check->current_step->list == head)
				break;
		} /* end 'send' */
		else if (check->current_step->action == TCPCHK_ACT_EXPECT) {
			if (unlikely(check->result == CHK_RES_FAILED))
				goto out_end_tcpcheck;

			if (conn->xprt->rcv_buf(conn, check->bi, check->bi->size) <= 0) {
				if (conn->flags & (CO_FL_ERROR | CO_FL_SOCK_RD_SH | CO_FL_DATA_RD_SH)) {
					done = 1;
					if ((conn->flags & CO_FL_ERROR) && !check->bi->i) {
						/* Report network errors only if we got no other data. Otherwise
						 * we'll let the upper layers decide whether the response is OK
						 * or not. It is very common that an RST sent by the server is
						 * reported as an error just after the last data chunk.
						 */
						chk_report_conn_err(conn, errno, 0);
						goto out_end_tcpcheck;
					}
				}
				else
					break;
			}

			/* mark the step as started */
			check->last_started_step = check->current_step;


			/* Intermediate or complete response received.
			 * Terminate string in check->bi->data buffer.
			 */
			if (check->bi->i < check->bi->size) {
				check->bi->data[check->bi->i] = '\0';
			}
			else {
				check->bi->data[check->bi->i - 1] = '\0';
				done = 1; /* buffer full, don't wait for more data */
			}

			contentptr = check->bi->data;

			/* Check that response body is not empty... */
			if (!check->bi->i) {
				if (!done)
					continue;

				/* empty response */
				step = tcpcheck_get_step_id(check);
				chunk_printf(&trash, "TCPCHK got an empty response at step %d", step);
				comment = tcpcheck_get_step_comment(check, step);
				if (comment)
					chunk_appendf(&trash, " comment: '%s'", comment);
				set_server_check_status(check, HCHK_STATUS_L7RSP, trash.str);

				goto out_end_tcpcheck;
			}

			if (!done && (check->current_step->string != NULL) && (check->bi->i < check->current_step->string_len) )
				continue; /* try to read more */

		tcpcheck_expect:
			if (check->current_step->string != NULL)
				ret = my_memmem(contentptr, check->bi->i, check->current_step->string, check->current_step->string_len) != NULL;
			else if (check->current_step->expect_regex != NULL)
				ret = regex_exec(check->current_step->expect_regex, contentptr);

			if (!ret && !done)
				continue; /* try to read more */

			/* matched */
			step = tcpcheck_get_step_id(check);
			if (ret) {
				/* matched but we did not want to => ERROR */
				if (check->current_step->inverse) {
					/* we were looking for a string */
					if (check->current_step->string != NULL) {
						chunk_printf(&trash, "TCPCHK matched unwanted content '%s' at step %d",
						             check->current_step->string, step);
					}
					else {
					/* we were looking for a regex */
						chunk_printf(&trash, "TCPCHK matched unwanted content (regex) at step %d", step);
					}
					comment = tcpcheck_get_step_comment(check, step);
					if (comment)
						chunk_appendf(&trash, " comment: '%s'", comment);
					set_server_check_status(check, HCHK_STATUS_L7RSP, trash.str);
					goto out_end_tcpcheck;
				}
				/* matched and was supposed to => OK, next step */
				else {
					/* allow next rule */
					check->current_step = LIST_NEXT(&check->current_step->list, struct tcpcheck_rule *, list);

					/* bypass all comment rules */
					while (&check->current_step->list != head &&
					       check->current_step->action == TCPCHK_ACT_COMMENT)
						check->current_step = LIST_NEXT(&check->current_step->list, struct tcpcheck_rule *, list);

					if (&check->current_step->list == head)
						break;

					if (check->current_step->action == TCPCHK_ACT_EXPECT)
						goto tcpcheck_expect;
					__conn_data_stop_recv(conn);
				}
			}
			else {
			/* not matched */
				/* not matched and was not supposed to => OK, next step */
				if (check->current_step->inverse) {
					/* allow next rule */
					check->current_step = LIST_NEXT(&check->current_step->list, struct tcpcheck_rule *, list);

					/* bypass all comment rules */
					while (&check->current_step->list != head &&
					       check->current_step->action == TCPCHK_ACT_COMMENT)
						check->current_step = LIST_NEXT(&check->current_step->list, struct tcpcheck_rule *, list);

					if (&check->current_step->list == head)
						break;

					if (check->current_step->action == TCPCHK_ACT_EXPECT)
						goto tcpcheck_expect;
					__conn_data_stop_recv(conn);
				}
				/* not matched but was supposed to => ERROR */
				else {
					/* we were looking for a string */
					if (check->current_step->string != NULL) {
						chunk_printf(&trash, "TCPCHK did not match content '%s' at step %d",
						             check->current_step->string, step);
					}
					else {
					/* we were looking for a regex */
						chunk_printf(&trash, "TCPCHK did not match content (regex) at step %d",
								step);
					}
					comment = tcpcheck_get_step_comment(check, step);
					if (comment)
						chunk_appendf(&trash, " comment: '%s'", comment);
					set_server_check_status(check, HCHK_STATUS_L7RSP, trash.str);
					goto out_end_tcpcheck;
				}
			}
		} /* end expect */
	} /* end loop over double chained step list */

	/* We're waiting for some I/O to complete, we've reached the end of the
	 * rules, or both. Do what we have to do, otherwise we're done.
	 */
	if (&check->current_step->list == head && !check->bo->o) {
		set_server_check_status(check, HCHK_STATUS_L7OKD, "(tcp-check)");
		goto out_end_tcpcheck;
	}

	/* warning, current_step may now point to the head */
	if (check->bo->o)
		__conn_data_want_send(conn);

	if (&check->current_step->list != head &&
	    check->current_step->action == TCPCHK_ACT_EXPECT)
		__conn_data_want_recv(conn);
	return;

 out_end_tcpcheck:
	/* collect possible new errors */
	if (conn->flags & CO_FL_ERROR)
		chk_report_conn_err(conn, 0, 0);

	/* cleanup before leaving */
	check->current_step = NULL;

	if (check->result == CHK_RES_FAILED)
		conn->flags |= CO_FL_ERROR;

	__conn_data_stop_both(conn);
	return;
}

const char *init_check(struct check *check, int type)
{
	check->type = type;

	/* Allocate buffer for requests... */
	if ((check->bi = calloc(sizeof(struct buffer) + global.tune.chksize, sizeof(char))) == NULL) {
		return "out of memory while allocating check buffer";
	}
	check->bi->size = global.tune.chksize;

	/* Allocate buffer for responses... */
	if ((check->bo = calloc(sizeof(struct buffer) + global.tune.chksize, sizeof(char))) == NULL) {
		return "out of memory while allocating check buffer";
	}
	check->bo->size = global.tune.chksize;

	/* Allocate buffer for partial results... */
	if ((check->conn = calloc(1, sizeof(struct connection))) == NULL) {
		return "out of memory while allocating check connection";
	}

	check->conn->t.sock.fd = -1; /* no agent in progress yet */

	return NULL;
}

void free_check(struct check *check)
{
	free(check->bi);
	free(check->bo);
	free(check->conn);
}

void email_alert_free(struct email_alert *alert)
{
	struct tcpcheck_rule *rule, *back;

	if (!alert)
		return;

	list_for_each_entry_safe(rule, back, &alert->tcpcheck_rules, list)
		free(rule);
	free(alert);
}

static struct task *process_email_alert(struct task *t)
{
	struct check *check = t->context;
	struct email_alertq *q;

#ifdef __VMS
	q = container_of(check, __typeof__(*q), check);
#else
	q = container_of(check, typeof(*q), check);
#endif

	if (!(check->state & CHK_ST_ENABLED)) {
		if (LIST_ISEMPTY(&q->email_alerts)) {
			/* All alerts processed, delete check */
			task_delete(t);
			task_free(t);
			check->task = NULL;
			return NULL;
		} else {
			struct email_alert *alert;

#ifdef __VMS
			alert = LIST_NEXT(&q->email_alerts, __typeof__(alert), list);
#else
			alert = LIST_NEXT(&q->email_alerts, typeof(alert), list);
#endif
			check->tcpcheck_rules = &alert->tcpcheck_rules;
			LIST_DEL(&alert->list);

			check->state |= CHK_ST_ENABLED;
		}

	}

	process_chk(t);

	if (!(check->state & CHK_ST_INPROGRESS) && check->tcpcheck_rules) {
		struct email_alert *alert;

#ifdef __VMS
		alert = container_of(check->tcpcheck_rules, __typeof__(*alert), tcpcheck_rules);
#else
		alert = container_of(check->tcpcheck_rules, typeof(*alert), tcpcheck_rules);
#endif
		email_alert_free(alert);

		check->tcpcheck_rules = NULL;
		check->state &= ~CHK_ST_ENABLED;
	}
	return t;
}

static int init_email_alert_checks(struct server *s)
{
	int i;
	struct mailer *mailer;
	const char *err_str;
	struct proxy *p = s->proxy;

	if (p->email_alert.queues)
		/* Already initialised, nothing to do */
		return 1;

	p->email_alert.queues = calloc(p->email_alert.mailers.m->count, sizeof *p->email_alert.queues);
	if (!p->email_alert.queues) {
		err_str = "out of memory while allocating checks array";
		goto error_alert;
	}

	for (i = 0, mailer = p->email_alert.mailers.m->mailer_list;
	     i < p->email_alert.mailers.m->count; i++, mailer = mailer->next) {
		struct email_alertq *q = &p->email_alert.queues[i];
		struct check *check = &q->check;


		LIST_INIT(&q->email_alerts);

		check->inter = p->email_alert.mailers.m->timeout.mail;
		check->rise = DEF_AGENT_RISETIME;
		check->fall = DEF_AGENT_FALLTIME;
		err_str = init_check(check, PR_O2_TCPCHK_CHK);
		if (err_str) {
			goto error_free;
		}

		check->xprt = mailer->xprt;
		if (!get_host_port(&mailer->addr))
			/* Default to submission port */
			check->port = 587;
		check->addr = mailer->addr;
		check->server = s;
	}

	return 1;

error_free:
	while (i-- > 1)
		task_free(p->email_alert.queues[i].check.task);
	free(p->email_alert.queues);
	p->email_alert.queues = NULL;
error_alert:
	Alert("Email alert [%s] could not be initialised: %s\n", p->id, err_str);
	return 0;
}


static int add_tcpcheck_expect_str(struct list *list, const char *str)
{
	struct tcpcheck_rule *tcpcheck;

	tcpcheck = calloc(1, sizeof *tcpcheck);
	if (!tcpcheck)
		return 0;

	tcpcheck->action = TCPCHK_ACT_EXPECT;
	tcpcheck->string = strdup(str);
	if (!tcpcheck->string) {
		free(tcpcheck);
		return 0;
	}

	LIST_ADDQ(list, &tcpcheck->list);
	return 1;
}

static int add_tcpcheck_send_strs(struct list *list, const char * const *strs)
{
	struct tcpcheck_rule *tcpcheck;
	const char *in;
	char *dst;
	int i;

	tcpcheck = calloc(1, sizeof *tcpcheck);
	if (!tcpcheck)
		return 0;

	tcpcheck->action = TCPCHK_ACT_SEND;

	tcpcheck->string_len = 0;
	for (i = 0; strs[i]; i++)
		tcpcheck->string_len += strlen(strs[i]);

	tcpcheck->string = malloc(tcpcheck->string_len + 1);
	if (!tcpcheck->string) {
		free(tcpcheck);
		return 0;
	}

	dst = tcpcheck->string;
	for (i = 0; strs[i]; i++)
		for (in = strs[i]; (*dst = *in++); dst++);
	*dst = 0;

	LIST_ADDQ(list, &tcpcheck->list);
	return 1;
}

static int enqueue_one_email_alert(struct email_alertq *q, const char *msg)
{
	struct email_alert *alert = NULL;
	struct tcpcheck_rule *tcpcheck;
	struct check *check = &q->check;
	struct proxy *p = check->server->proxy;

	alert = calloc(1, sizeof *alert);
	if (!alert) {
		goto error;
	}
	LIST_INIT(&alert->tcpcheck_rules);

	tcpcheck = calloc(1, sizeof *tcpcheck);
	if (!tcpcheck)
		goto error;
	tcpcheck->action = TCPCHK_ACT_CONNECT;
	LIST_ADDQ(&alert->tcpcheck_rules, &tcpcheck->list);

	if (!add_tcpcheck_expect_str(&alert->tcpcheck_rules, "220 "))
		goto error;

	{
		const char * const strs[4] = { "EHLO ", p->email_alert.myhostname, "\r\n" };
		if (!add_tcpcheck_send_strs(&alert->tcpcheck_rules, strs))
			goto error;
	}

	if (!add_tcpcheck_expect_str(&alert->tcpcheck_rules, "250 "))
		goto error;

	{
		const char * const strs[4] = { "MAIL FROM:<", p->email_alert.from, ">\r\n" };
		if (!add_tcpcheck_send_strs(&alert->tcpcheck_rules, strs))
			goto error;
	}

	if (!add_tcpcheck_expect_str(&alert->tcpcheck_rules, "250 "))
		goto error;

	{
		const char * const strs[4] = { "RCPT TO:<", p->email_alert.to, ">\r\n" };
		if (!add_tcpcheck_send_strs(&alert->tcpcheck_rules, strs))
			goto error;
	}

	if (!add_tcpcheck_expect_str(&alert->tcpcheck_rules, "250 "))
		goto error;

	{
		const char * const strs[2] = { "DATA\r\n" };
		if (!add_tcpcheck_send_strs(&alert->tcpcheck_rules, strs))
			goto error;
	}

	if (!add_tcpcheck_expect_str(&alert->tcpcheck_rules, "354 "))
		goto error;

	{
		struct tm tm;
		char datestr[48];
		const char * const strs[18] = {
			"From: ", p->email_alert.from, "\r\n",
			"To: ", p->email_alert.to, "\r\n",
			"Date: ", datestr, "\r\n",
			"Subject: [HAproxy Alert] ", msg, "\r\n",
			"\r\n",
			msg, "\r\n",
			"\r\n",
			".\r\n",
			NULL
		};

		get_localtime(date.tv_sec, &tm);

		if (strftime(datestr, sizeof(datestr), "%a, %d %b %Y %T %z (%Z)", &tm) == 0) {
			goto error;
		}

		if (!add_tcpcheck_send_strs(&alert->tcpcheck_rules, strs))
			goto error;
	}

	if (!add_tcpcheck_expect_str(&alert->tcpcheck_rules, "250 "))
		goto error;

	{
		const char * const strs[2] = { "QUIT\r\n" };
		if (!add_tcpcheck_send_strs(&alert->tcpcheck_rules, strs))
			goto error;
	}

	if (!add_tcpcheck_expect_str(&alert->tcpcheck_rules, "221 "))
		goto error;

	if (!check->task) {
		struct task *t;

		if ((t = task_new()) == NULL)
			goto error;

		check->task = t;
		t->process = process_email_alert;
		t->context = check;

		/* check this in one ms */
		t->expire = tick_add(now_ms, MS_TO_TICKS(1));
		check->start = now;
		task_queue(t);
	}

	LIST_ADDQ(&q->email_alerts, &alert->list);

	return 1;

error:
	email_alert_free(alert);
	return 0;
}

static void enqueue_email_alert(struct proxy *p, const char *msg)
{
	int i;
	struct mailer *mailer;

	for (i = 0, mailer = p->email_alert.mailers.m->mailer_list;
	     i < p->email_alert.mailers.m->count; i++, mailer = mailer->next) {
		if (!enqueue_one_email_alert(&p->email_alert.queues[i], msg)) {
			Alert("Email alert [%s] could not be enqueued: out of memory\n", p->id);
			return;
		}
	}

	return;
}

/*
 * Send email alert if configured.
 */
void send_email_alert(struct server *s, int level, const char *format, ...)
{
	va_list argp;
	char buf[1024];
	int len;
	struct proxy *p = s->proxy;

	if (!p->email_alert.mailers.m || level > p->email_alert.level ||
	    format == NULL || !init_email_alert_checks(s))
		return;

	va_start(argp, format);
	len = vsnprintf(buf, sizeof(buf), format, argp);
	va_end(argp);

	if (len < 0 || len >= sizeof(buf)) {
		Alert("Email alert [%s] could not format message\n", p->id);
		return;
	}

	enqueue_email_alert(p, buf);
}

/*
 * Return value:
 *   the port to be used for the health check
 *   0 in case no port could be found for the check
 */
int srv_check_healthcheck_port(struct check *chk)
{
	int i = 0;
	struct server *srv = NULL;

	srv = chk->server;

	/* If neither a port nor an addr was specified and no check transport
	 * layer is forced, then the transport layer used by the checks is the
	 * same as for the production traffic. Otherwise we use raw_sock by
	 * default, unless one is specified.
	 */
	if (!chk->port && !is_addr(&chk->addr)) {
#ifdef USE_OPENSSL
		chk->use_ssl |= (srv->use_ssl || (srv->proxy->options & PR_O_TCPCHK_SSL));
#endif
		chk->send_proxy |= (srv->pp_opts);
	}

	/* by default, we use the health check port ocnfigured */
	if (chk->port > 0)
		return chk->port;

	/* try to get the port from check_core.addr if check.port not set */
	i = get_host_port(&chk->addr);
	if (i > 0)
		return i;

	/* try to get the port from server address */
	/* prevent MAPPORTS from working at this point, since checks could
	 * not be performed in such case (MAPPORTS impose a relative ports
	 * based on live traffic)
	 */
	if (srv->flags & SRV_F_MAPPORTS)
		return 0;

	i = srv->svc_port; /* by default */
	if (i > 0)
		return i;

	return 0;
}


/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 * End:
 */
