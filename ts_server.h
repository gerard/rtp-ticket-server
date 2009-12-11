#ifndef _TS_SERVER_H
#define _TS_SERVER_H

#include <stdio.h>
#include "common.h"

// CONSTANTS
#define MESSAGESIZE 		256
#define LOGFILE 			"/dev/tty"
#define DUMPFILE			"reservations.dump.txt"
#define PORT 				1340
#define RECVTO 				180					// Timeout for recv() in seconds

// ERROR CODES
#define ERROR_NOSEND 		1
#define ERROR_RECVTO 		2
#define ERROR_FORMAT 		3
#define ERROR_TOOLONG 		4
#define ERROR_SOLDOUT		5
#define EXIT				0
#define NOERROR				-1

// Client reservations
struct cdb {
	int id;
	int pass;
	int canceled;
	struct ts *t;
	struct cdb *n;
};

int ts_getidinmsg(char **msg_iter);
int ts_getpassinmsg(char **msg_iter);
int ts_ismsgcomplete(char *s);

// Client database management
struct cdb *ts_cdb_add(struct cdb *clients, struct ts t);
struct ts *ts_cdb_cancel(struct cdb *clients, int id, int pass);
int ts_cdb_getid(struct cdb *clients);
int ts_cdb_getpass(struct cdb *clients);
int ts_cdb_ntickets(struct cdb *clients, int uid);
void ts_cdb_dump(struct cdb *clients, FILE *fd);
int ts_ismsgcomplete(char *s);
int ts_checkvalid(struct ts t, int ng, int nc);
int ts_ticketsavail(int **tickets, int ng, int nc);

#endif
