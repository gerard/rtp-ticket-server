#ifndef _TS_H
#define _TS_H

#define PORT 				1340
#define MESSAGESIZE 		256
#define MAXTICKETSPERUID	10

// ACTION CODES
#define ACTION_UNKNOWN		-1
#define ACTION_EXIT			0
#define ACTION_BUY			1
#define ACTION_CANCEL		2
#define ACTION_AVAILABLE	3
#define ACTION_EOK			4
#define ACTION_SORRY		5
#define ACTION_EXCEEDED		6
#define ACTION_END			7

// Ticket request structure
struct ts {
	int uid;
	int gig;
	int cat;
	int ntickets;
};

int ts_sendall(int s, char *buf, int len);
int ts_getticket(struct ts *t, char **msg_iter, int bool_withuid);
int ts_getaction(char **s);
int ts_islistcomplete(char *msg);
int ts_ismsgcomplete(char *s);
int ts_getnkinds(char *msg);

#endif
