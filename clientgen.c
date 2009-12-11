#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "common.h"

#define SERVER_IP				"127.0.0.1"
#define SERVER_PORT				1340

#define NCLIENTS_MEAN			   4000
#define NCLIENTS_STDD			   1000

#define CLI_P1						 10
#define CLI_P2						 50
#define CLI_P3						 20
#define CLI_P4						 10
#define CLI_P5						  8
#define CLI_P6						  2
#define CLI_NEXTRA					 10

#define WAITTIME_MEAN			8000000
#define WAITTIME_STDD			7000000
#define CREATIME_MEAN			 300000
#define CREATIME_STDD			 300000

struct param {
	int ntickets;
	int bool_mcat;
	int ncat;
};

void sleeprandtime(int mean, int stdd) {
	usleep(mean + (random()%(stdd*2)) - stdd);
}

int throwdice(int prob) {
	if((random()%prob) == 0) return 1;
	else return 0;
}

void *thread_client(void *arg) {
	struct param *p;
	struct ts *t;
	struct sockaddr_in server_addr;
	char *msg, *msg_iter, *s_tmp;
	int uid, r;
	int n_gigcat, nt_b, nb_recv = 0;
	int s;
	int i;
	int id, pass;
	
	p = (struct param *)arg;
	uid = random();
	
	if((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("Socket creation failed");
		raise(SIGKILL);
		pthread_exit(NULL);
	}
	
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(SERVER_PORT);
	server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
	memset(&(server_addr.sin_zero), '\0', 8);
	
	if(connect(s, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == -1) {
		perror("Connection failed");
		raise(SIGTERM);
		pthread_exit(NULL);
	}
	
	s_tmp = msg = (char *)malloc(MESSAGESIZE*sizeof(char));
	do {
		memset(s_tmp, 0, MESSAGESIZE);
		nb_recv = nb_recv + recv(s, s_tmp, MESSAGESIZE, 0);
		msg = realloc(msg, MESSAGESIZE + nb_recv);
		s_tmp = &(msg[nb_recv]);
	} while(!ts_islistcomplete(msg));
	
	n_gigcat = ts_getnkinds(msg);
	if(n_gigcat > 0) {	// We just try to buy if there is something left
		t = (struct ts *)malloc(n_gigcat*sizeof(struct ts));
	
		msg_iter = msg;
		i = 0;
		while(ts_getaction(&msg_iter) != ACTION_END) {
			while(ts_getticket(&(t[i]), &msg_iter, 0)) i++;
			msg_iter++;
		}
			
		if(p->bool_mcat == 0) {	// The client buys tickets in one category
			sleeprandtime(WAITTIME_MEAN, WAITTIME_STDD);
			r = random()%n_gigcat;
			sprintf(msg, "BUY %d:%c%d:%d\n", uid, (char)(t[r].gig + 'A'), t[r].cat, p->ntickets);
			ts_sendall(s, msg, strlen(msg));
		
			s_tmp = msg;
			nb_recv = 0;
			do {
				memset(s_tmp, 0, MESSAGESIZE);
				nb_recv = nb_recv + recv(s, s_tmp, MESSAGESIZE, 0);
				s_tmp = &(msg[nb_recv]);
			} while(!ts_ismsgcomplete(msg));	
		} else {
			nt_b = 0;
			for(i = 0; i < p->ncat; i++) {
				sleeprandtime(WAITTIME_MEAN, WAITTIME_STDD);
				r = random()%n_gigcat;
			
				if(i == 0) nt_b = p->ntickets - p->ncat + 1;
				else nt_b = 1;
				sprintf(msg, "BUY %d:%c%d:%d\n", uid, (char)(t[r].gig + 'A'), t[r].cat, nt_b);
				ts_sendall(s, msg, strlen(msg));

				s_tmp = msg;
				nb_recv = 0;
				do {
					memset(s_tmp, 0, MESSAGESIZE);
					nb_recv = nb_recv + recv(s, s_tmp, MESSAGESIZE, 0);
					s_tmp = &(msg[nb_recv]);
				} while(!ts_ismsgcomplete(msg));
			}
		}
	}
	sleeprandtime(WAITTIME_MEAN, WAITTIME_STDD);
	
	// Let's make a cancelation
	msg_iter = msg;
	if(throwdice(15) && ts_getaction(&msg_iter) == ACTION_EOK && n_gigcat > 0) {
		sscanf(msg_iter, "%d:%d\n", &id, &pass);
		sprintf(msg, "CAN %d:%d\n", id, pass);
		ts_sendall(s, msg, strlen(msg));
		
		s_tmp = msg;
		nb_recv = 0;
		do {
			memset(s_tmp, 0, MESSAGESIZE);
			nb_recv = nb_recv + recv(s, s_tmp, MESSAGESIZE, 0);
			s_tmp = &(msg[nb_recv]);
		} while(!ts_ismsgcomplete(msg));
	}
	
	// Now we could decide to do something different if no more tickets are available
	// Anyway, we just exit
	ts_sendall(s, "EXI\n", strlen("EXI\n"));
	
	// Freeing resources
	free(msg);
	free(p);
	free(t);
	close(s);
	pthread_exit(NULL);
}

pthread_t gen_client(int ntickets, int bool_mcat, int ncat) {
	struct param *p;
	pthread_t thread;
	
	p = (struct param *)malloc(sizeof(struct param));
	p->ntickets = ntickets;
	p->bool_mcat = bool_mcat;
	p->ncat = ncat;
	
	//fprintf(stderr, "New: %2d, %d, %d\n", ntickets, bool_mcat, ncat);
	pthread_create(&thread, NULL, thread_client, p);
	return thread;
}

int main(int argc, char *argv[]) {
	pthread_t *threads;
	int nclients, i, r, bool_m;

	srandom(clock());
	if(argc == 1) { // Mono-client mode
		pthread_join(gen_client(10, 1, 4), NULL);
		return 0;
	}
	
	nclients = NCLIENTS_MEAN + (random()%(NCLIENTS_STDD*2)) - NCLIENTS_STDD;
	threads = (pthread_t *)malloc(nclients*sizeof(pthread_t));

	for(i = 0; i < nclients; i++) {
		sleeprandtime(CREATIME_MEAN, CREATIME_STDD);
		r = random()%100;
		
		if((r = r - CLI_P1) < 0) {
			threads[i] = gen_client(1, 0, 0); 
			continue;
		}
		
		if((r = r - CLI_P2) < 0) {
			bool_m = throwdice(10);
			threads[i] = gen_client(2, bool_m, 2); 
			continue;
		}
		if((r = r - CLI_P3) < 0) {
			bool_m = throwdice(10);
			threads[i] = gen_client(3, bool_m, random()%2 + 2);
			continue;
		}
		if((r = r - CLI_P4) < 0) {
			bool_m = throwdice(10);
			threads[i] = gen_client(4, bool_m, random()%3 + 2);
			continue;
		}
		if((r = r - CLI_P5) < 0) {
			bool_m = throwdice(5);
			threads[i] = gen_client(5, bool_m, random()%4 + 2);
			continue;
		}
		bool_m = throwdice(2);
		threads[i] = gen_client(6 + random()%CLI_NEXTRA, bool_m, random()%5 + 2);
	}

	fprintf(stderr, "All clients created, waiting for them\n");
	for(i = 0; i < nclients; i++) pthread_join(threads[i], NULL);

	return 0;
}
