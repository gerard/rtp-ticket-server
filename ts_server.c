#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <signal.h>
#include "ts_server.h"
#include "common.h"

// Static internal variables
static int id;

int ts_getidinmsg(char **msg_iter) {
	char *msg, s[MESSAGESIZE];
	int i;
	
	msg = *msg_iter;
	
	for(i = 0; isdigit(msg[0]); i++, msg++) s[i] = msg[0];
	s[i] = '\0';
	
	if(i == 0 || msg[0] != ':') return 0;
	msg++;
	
	*msg_iter = msg;
	return atoi(s);
}

int ts_getpassinmsg(char **msg_iter) {
	char *msg, s[MESSAGESIZE];
	int i;
	
	msg = *msg_iter;
	
	for(i = 0; isdigit(msg[0]); i++, msg++) s[i] = msg[0];
	s[i] = '\0';

	// Return -1 is safe because random() doesn't cover the negative numbers
	if(i == 0) return -1;
	
	*msg_iter = msg;
	return atoi(s);
}

int ts_checkvalid(struct ts t, int ng, int nc) {
	if(t.gig >= ng) return 0;
	if(t.cat >= nc) return 0;
	if(t.gig < 0) return 0;
	if(t.cat < 0) return 0;
	if(t.ntickets <= 0) return 0;
	return 1;
}

int ts_ticketsavail(int **tickets, int ng, int nc) {
	int i, j;
	
	for(i = 0; i < ng; i++) {
		for(j = 0; j < nc; j++) { 
			if(tickets[i][j]) return 1;
		}
	}
	
	return 0;
}

// Client management: Basically a linked list with insertion in the beginning.
// However, we have to be careful with these functions, because they are not
// thread-safe

struct cdb *ts_cdb_add(struct cdb *clients, struct ts t) {
	struct cdb *new;
	srandom(clock());
	
	if(clients == NULL) id = 1;		// First adding to the DB
	
	new = (struct cdb *)malloc(sizeof(struct cdb));
	new->t = (struct ts *)malloc(sizeof(struct ts));
	
	new->id = id++;
	new->pass = random();
	new->canceled = 0;
	new->n = clients;
	memcpy(new->t, &t, sizeof(struct ts));
	
	return new;
}

struct ts *ts_cdb_cancel(struct cdb *clients, int id, int pass) {
	while(clients != NULL && clients->id != id) clients = clients->n;
	if(clients != NULL && clients->pass == pass && clients->canceled == 0) {
		clients->canceled = 1;
		return clients->t;
	}
	else return NULL;
}

// CAREFUL: This only works if ts_cdb_add() and ts_cdb_get*() are made atomically
int ts_cdb_getpass(struct cdb *clients) {
	return clients->pass;
}

int ts_cdb_getid(struct cdb *clients) {
	return clients->id;
}

int ts_cdb_ntickets(struct cdb *clients, int uid) {
	int n = 0;
	while(clients) {
		if(clients->t->uid == uid && !clients->canceled) n = n + clients->t->ntickets;
		clients = clients->n;
	}
	
	return n;
}

void ts_cdb_dump(struct cdb *clients, FILE *fd) {	
	while(clients) {
		if(clients->canceled) fprintf(fd, "CAN %d:%d\n", clients->t->uid, clients->id);
		else fprintf(fd, "BUY %d:%c%d:%d:%d\n", clients->t->uid, (char)(clients->t->gig + 'A'), clients->t->cat,
											  clients->t->ntickets, clients->id);
		clients = clients->n;
	}
}
