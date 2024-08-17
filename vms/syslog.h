/*
**
** � Copyright 2002 Hewlett-Packard Development Company, L.P.
**
** Hewlett-Packard and the Hewlett-Packard logo are trademarks
** of Hewlett-Packard Development Company L.P. in the U.S. and/or
** other countries.
**
** Confidential computer software.
** Valid license from Hewlett-Packard required for possession, use
** or copying.  Consistent with FAR 12.211 and 12.212, Commercial
** Computer Software, Computer Software Documentation, and Technical
** Data for Commercial.  Items are licensed to the U.S. Government
** under vendor's standard commercial license.
**
** Hewlett-Packard shall not be liable for technical or editorial
** errors or omissions contained herein.  The information is provided
** "as is" without warranty of any kind and is subject to change
** without notice.  The warranties for Hewlett-Packard products are
** set forth in the express warranty statements accompanying such
** products.  Nothing herein should be construed as constituting an
** additional warranty.
**
*/

#ifndef __SYSLOG_H
#define __SYSLOG_H 1

#define LOG_KERN	(0<<3)	/* kernel messages 			    */
#define LOG_USER	(1<<3)	/* random user-level messages 		    */
#define LOG_MAIL	(2<<3)	/* mail system 				    */
#define LOG_DAEMON	(3<<3)	/* system daemons 			    */
#define LOG_AUTH	(4<<3)	/* security/authorization messages 	    */
#define LOG_SYSLOG	(5<<3)	/* messages generated internally by syslogd */
#define LOG_LPR		(6<<3)	/* line printer subsystem 		    */
#define LOG_NEWS	(7<<3)	/* network news subsystem 		    */
#define LOG_UUCP	(8<<3)	/* UUCP subsystem 			    */
#define	LOG_CRON	(9<<3)	/* clock daemon 			    */
#define LOG_LOCAL0	(16<<3)	/* reserved for local use 		    */
#define LOG_LOCAL1	(17<<3)	/* reserved for local use 		    */
#define LOG_LOCAL2	(18<<3)	/* reserved for local use 		    */
#define LOG_LOCAL3	(19<<3)	/* reserved for local use 		    */
#define LOG_LOCAL4	(20<<3)	/* reserved for local use 		    */
#define LOG_LOCAL5	(21<<3)	/* reserved for local use 		    */
#define LOG_LOCAL6	(22<<3)	/* reserved for local use 		    */
#define LOG_LOCAL7	(23<<3)	/* reserved for local use 		    */

#define LOG_NFACILITIES	24	/* maximum number of facilities 	    */
#define LOG_FACMASK	0x03f8	/* mask to extract facility part 	    */
#define LOG_FAC(p)	(((p) & LOG_FACMASK) >> 3)	/* extract facility */

/*
** Priorities (these are ordered)
*/
#define LOG_EMERG	0	/* system is unusable 			    */
#define LOG_ALERT	1	/* action must be taken immediately 	    */
#define LOG_CRIT	2	/* critical conditions 			    */
#define LOG_ERR		3	/* error conditions 			    */
#define LOG_WARNING	4	/* warning conditions 			    */
#define LOG_NOTICE	5	/* normal but signification condition 	    */
#define LOG_INFO	6	/* informational 			    */
#define LOG_DEBUG	7	/* debug-level messages 		    */

#define LOG_PRIMASK	0x0007	/* mask to extract priority part (internal) */
#define LOG_PRI(p)	((p) & LOG_PRIMASK)		/* extract priority */
#define	LOG_MAKEPRI(fac, pri)	(((fac) << 3) | (pri))

/*
** arguments to setlogmask.
*/
#define	LOG_MASK(pri)	(1 << (pri))		/* mask for one priority     */
#define	LOG_UPTO(pri)	((1 << ((pri)+1)) - 1)	/* all priorities through pri*/

/*
 *  Option flags for openlog.
 *
 *	LOG_ODELAY no longer does anything; LOG_NDELAY is the
 *	inverse of what it used to be.
 */
#define	LOG_PID		0x01	/* log the pid with each message */
#define	LOG_CONS	0x02	/* log on the console if errors in sending */
#define	LOG_ODELAY	0x04	/* delay open until syslog() is called */
#define LOG_NDELAY	0x08	/* don't delay open */
#define LOG_NOWAIT	0x10	/* if forking to log on console, don't wait() */
#define LOG_PERROR	0x20	/* print log message also to standard error */

void openlog (const char *, int, int);
void syslog (int, const char *, ...);
void closelog (void);
int setlogmask (int);

#endif /* __SYSLOG_H */
