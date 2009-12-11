#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "common.h"

// SENDALL PROCEDURE
// the standard send() call doesn't guarantee to deliver all the data. This procedure gives that functionality
int ts_sendall(int s, char *buf, int len) {
    int total = 0;        							// How many bytes we've sent
    int bytesleft = len; 							// How many we have left to send
    int n;

    while(total < len) {
        n = send(s, buf+total, bytesleft, 0);
        if (n == -1) break;
        total += n;
        bytesleft -= n;
    }

	if(n == -1) return -1;
	else return 0;
}

// This is tricky. At the same time that we are reading the message to fill the structure, we want
// to advance the pointer of the message so iterative calls to the function will take the message
// were the previous calls finished, to achieve this we need to give an indirection to the string,
// so that's the reason we are using char **.
// Notice also the return points of the functions, that are checking for the format of the client.
int ts_getticket(struct ts *t, char **msg_iter, int bool_withuid) {
	char s[MESSAGESIZE];
	char *msg;
	int i;
	
	if(*msg_iter[0] == '\0') return 0;		// Empty string
	msg = *msg_iter;
	
	// Only used when called from the server
	if(bool_withuid) {
		for(i = 0; isdigit(msg[0]); i++, msg++) s[i] = msg[0];
		s[i] = '\0';
	
		if(i == 0 || msg[0] != ':') return 0;	// Not a valid format
		msg++;

		t->uid = atoi(s);
	}
	
	// Gig
	if(!isupper(msg[0])) return 0;			// Not a valid format
	t->gig = (int)msg[0] - 'A';
	msg++;
	
	// Category
	for(i = 0; isdigit(msg[0]); i++, msg++) s[i] = msg[0];

	if(i == 0) return 0;					// Not a valid format
	s[i] = '\0';
	t->cat = atoi(s);
	
	if(msg[0] != ':') return 0;				// Not a valid format
	
	// Number of tickets
	msg++;
	for(i = 0; isdigit(msg[0]); i++, msg++) s[i] = msg[0];
	
	if(i == 0) return 0;					// Not a valid format
	s[i] = '\0';
	t->ntickets = atoi(s);
	msg++;
	
	*msg_iter = msg;
	return 1;								// Success
}

int ts_getaction(char **s) {
	char *comp;
	
	// This advances the string to the next data
	comp = *s;
	*s = &(comp[4]);
	
	if(strncmp(comp, "EXI", 3) == 0) return ACTION_EXIT;
	if(strncmp(comp, "BUY", 3) == 0) return ACTION_BUY;
	if(strncmp(comp, "CAN", 3) == 0) return ACTION_CANCEL;
	if(strncmp(comp, "AVA", 3) == 0) return ACTION_AVAILABLE;
	if(strncmp(comp, "EOK", 3) == 0) return ACTION_EOK;
	if(strncmp(comp, "SRY", 3) == 0) return ACTION_SORRY;
	if(strncmp(comp, "EXC", 3) == 0) return ACTION_EXCEEDED;
	if(strncmp(comp, "END", 3) == 0) return ACTION_END;
	
	return ACTION_UNKNOWN;
}

int ts_islistcomplete(char *msg) {
	if(msg[0] == '\0') return 1;
	if(strncmp(&(msg[strlen(msg) - 4]), "END\n", 4) == 0) return 1;
	else return 0;
}

int ts_ismsgcomplete(char *s) {
	if(s[0] == 0) return 1;
	if(s[strlen(s)-1] == '\n') return 1;
	else return 0;
}

char *ts_getline(char *s, int nline) {
	int i, j;
	char *l;
	
	l = s;
	for(i = 0, j = 0; i < nline; i++) {
		while(s[j] != '\n') j++;
		j++;
		l = &(s[j]);
	}

	return l;
}

int ts_getnkinds(char *msg) {
	int i, n = 0;
	
	for(i = 0; msg[i] != '\0'; i++) if(msg[i] == ':') n++;
	return n;
}

int ts_getncats(char *msg) {
	int i, n = 0;
	
	for(i = 0; msg[i] != '\0'; i++) if(msg[i] == 'A') n++;
	return n;
}

int ts_getgigs(char *msg) {
	return ts_getnkinds(msg)/ts_getncats(msg);
}
