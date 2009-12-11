#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "common.h"
#include "ts_server.h"

// Global Variables
int inc_socket;
int ng, nc;											// Number of gigs and categories
int **tickets;										// Tickets[ng][nc]
FILE *log_fd;										// Log file descriptor
struct cdb *clients;
pthread_mutex_t log_mutex, selling;					// Protection Mutexes for Global Variables
													// selling includes protection for the client database
													// and the ticket matrix

static void close_mainsocket(int sig_no) {
	if(sig_no == SIGINT || sig_no == SIGTERM) close(inc_socket);
}

// SERVICE EXIT PROCEDURE
// Basically, closing the socket, reporting and exiting the thread
void service_exit(int socket, struct sockaddr_in addr, int err) {
	char msg[MESSAGESIZE];	
	close(socket);
	
	switch(err) {
	case ERROR_NOSEND:
		strncpy(msg, "BAD %s:%d send() denied\n", MESSAGESIZE);
		break;
	case ERROR_RECVTO:
		strncpy(msg, "BAD %s:%d recv() too late\n", MESSAGESIZE);
		break;
	case ERROR_FORMAT:
		strncpy(msg, "BAD %s:%d Unknown format\n", MESSAGESIZE);
		break;
	case ERROR_TOOLONG:
		strncpy(msg, "BAD %s:%d Too long message\n", MESSAGESIZE);
		break;
	case EXIT:
		strncpy(msg, "EXI %s:%d\n", MESSAGESIZE);
		break;
	case NOERROR:
	default:
		strncpy(msg, "BAD %s:%d Unknown error\n", MESSAGESIZE);
		break;
	}

	pthread_mutex_lock(&log_mutex);
	fprintf(log_fd, msg, inet_ntoa(addr.sin_addr), addr.sin_port);
	pthread_mutex_unlock(&log_mutex);	
	
	pthread_exit(NULL);
}

// SERVICE THREAD
void *service(void *arg) {
	struct sockaddr_in remote_addr;							// Remote Address structure for logging purpouses
	socklen_t remote_addr_size = sizeof(struct sockaddr);	// Remote Address structure length
	struct ts *ts_can, ts_sell;
	char msg[MESSAGESIZE], *msg_iter;
	char s_tmp[MESSAGESIZE];
	int socket;
	int i, j;
	int id, pass;											// ID and PASS of the transaction
	int ntickets;											// 
	
	// The socket is provided by the main thread
	socket = *(int *)arg;
	free(arg);												// No longer needed, so the sooner the better
	
	// We store the connecting address
	getpeername(socket, (struct sockaddr *) &remote_addr, &remote_addr_size);
	pthread_mutex_lock(&log_mutex);
	fprintf(log_fd, "CON %s:%d\n", inet_ntoa(remote_addr.sin_addr), remote_addr.sin_port);
	pthread_mutex_unlock(&log_mutex);

	// If no more tickets are available, we skip the AVA section
	if(ts_ticketsavail(tickets, ng, nc)) {
		strcpy(msg, "AVA ");
		for(i = 0; i < ng; i++) {
			for(j = 0; j < nc; j++) {
				if(tickets[i][j] > 0) { // Tickets available
					snprintf(s_tmp, MESSAGESIZE, "%c%d:%d ", (char)('A' + i), j, tickets[i][j]);
					if((strlen(s_tmp) + strlen(msg)) >= MESSAGESIZE) { // Too long msg: send and create another
						strcat(msg, "\n");
						if(ts_sendall(socket, msg, strlen(msg)) < 0) service_exit(socket, remote_addr, ERROR_NOSEND);
						strcpy(msg, "AVA ");
					}
					strcat(msg, s_tmp);
				}
			}
		}
	
		strcat(msg, "\n");
		if(ts_sendall(socket, msg, strlen(msg)) < 0) service_exit(socket, remote_addr, ERROR_NOSEND);
	}
	
	if(ts_sendall(socket, "END\n\0", strlen("END\n")) < 0) service_exit(socket, remote_addr, ERROR_NOSEND);
	
	// MAIN SERVICE LOOP: The client already have the availability, and now it's time for his requests.
	// If another client buys the tickets showed while this client makes the decision and we run out of
	// tickets, we will show the client the amount left.
	// This loop ends when the user sends an exit (EXI) request. It also can finnish in some controlled
	// error conditions. All this is managed by the service_exit procedure.
	while(1) {
		msg[0]='\0'; // This is just a way to delete the string
		do { // Receiving the data
			// We do this because clients (like telnet does) may not use '\0' as string delimiter
			memset(s_tmp, 0, MESSAGESIZE);
			if(recv(socket, s_tmp, MESSAGESIZE, 0) == -1) {
				if(errno == EAGAIN) service_exit(socket, remote_addr, ERROR_RECVTO);
				else {
					perror("recv");
					pthread_exit(NULL);
				}
			}
			if((strlen(s_tmp) + strlen(msg)) >= MESSAGESIZE) service_exit(socket, remote_addr, ERROR_TOOLONG);
			strcat(msg, s_tmp);
		} while(!ts_ismsgcomplete(msg));
	
		// Processing the data
		msg_iter = msg;		// We will iterate along the string, so we save the original point of the msg
		switch(ts_getaction(&msg_iter)) {
		case ACTION_UNKNOWN:
			service_exit(socket, remote_addr, ERROR_FORMAT);
			break;
		case ACTION_EXIT:
			service_exit(socket, remote_addr, EXIT);
			break;
		case ACTION_BUY:
			if(ts_getticket(&ts_sell, &msg_iter, 1) && ts_checkvalid(ts_sell, ng, nc)) {
				// LOCKING MUTEX
				pthread_mutex_lock(&selling);
				if(tickets[ts_sell.gig][ts_sell.cat] < ts_sell.ntickets) { // Not enough tickets
					pthread_mutex_unlock(&selling); // END OF MUTEX (IF)
			
					// Not enough tickets available, we reply with the amount left
					sprintf(msg, "SRY %c%d:%d\n", (char)('A' + ts_sell.gig), ts_sell.cat, tickets[ts_sell.gig][ts_sell.cat]);
					if(ts_sendall(socket, msg, strlen(msg)) < 0) service_exit(socket, remote_addr, ERROR_NOSEND);
				} 
				else { // We control that the user (uid) is not buying more tickets than is allowed
					ntickets = ts_cdb_ntickets(clients, ts_sell.uid);
					if(ntickets + ts_sell.ntickets > MAXTICKETSPERUID) {
						pthread_mutex_unlock(&selling); // END OF MUTEX (ELSE-IF)
						
						// Reporting to the log
						pthread_mutex_lock(&log_mutex);
						fprintf(log_fd, "EXC %s:%d:%d", inet_ntoa(remote_addr.sin_addr), remote_addr.sin_port, 0);
						fprintf(log_fd, ":%c%d:%d\n", (char)('A' + ts_sell.gig), ts_sell.cat, ts_sell.ntickets);
						pthread_mutex_unlock(&log_mutex);
						sprintf(msg, "EXC %d:%d\n", ntickets, MAXTICKETSPERUID);
						if(ts_sendall(socket, msg, strlen(msg)) < 0) service_exit(socket, remote_addr, ERROR_NOSEND);
					} else { // Sold without problems
						tickets[ts_sell.gig][ts_sell.cat] = tickets[ts_sell.gig][ts_sell.cat] - ts_sell.ntickets;
			
						// Reporting the database and getting ID and PASS for future modifications
						clients = ts_cdb_add(clients, ts_sell);
						id = ts_cdb_getid(clients);
						pass = ts_cdb_getpass(clients);
						pthread_mutex_unlock(&selling); // END OF MUTEX (ELSE-ELSE)
			
						sprintf(msg, "EOK %d:%d\n", id, pass);
						if(ts_sendall(socket, msg, strlen(msg)) < 0) service_exit(socket, remote_addr, ERROR_NOSEND);
			
						// Reporting to the log
						pthread_mutex_lock(&log_mutex);
						fprintf(log_fd, "BUY %s:%d:%d", inet_ntoa(remote_addr.sin_addr), remote_addr.sin_port, id);
						fprintf(log_fd, ":%c%d:%d\n", (char)('A' + ts_sell.gig), ts_sell.cat, ts_sell.ntickets);
						pthread_mutex_unlock(&log_mutex);
					}
				} // Notice that the mutex is unlocked (either on the if or in the else)
			} else { // Bad ticket format: exiting.
				service_exit(socket, remote_addr, ERROR_FORMAT);
			}
			break;
		case ACTION_CANCEL:
			msg_iter = &(msg[4]);
			id = ts_getidinmsg(&msg_iter);
			pass = ts_getpassinmsg(&msg_iter);
			
			// Note that we don't delete the entry of the database, we just set it to canceled
			// This can be useful if we want to charge some money for the service
			if(id && pass != -1) {	
				pthread_mutex_lock(&selling);		// CDB MUTEX LOCK
				ts_can = ts_cdb_cancel(clients, id, pass);
				if(ts_can) {
					// Reporting the tickets matrix
					tickets[ts_can->gig][ts_can->cat] = tickets[ts_can->gig][ts_can->cat] + ts_can->ntickets;
					pthread_mutex_unlock(&selling);
					
					// Reporting the log
					pthread_mutex_lock(&log_mutex);
					fprintf(log_fd, "DEL %s:%d:%d", inet_ntoa(remote_addr.sin_addr), remote_addr.sin_port, id);
					fprintf(log_fd, ":%c%d:%d\n", (char)('A' + ts_can->gig), ts_can->cat, ts_can->ntickets);
					pthread_mutex_unlock(&log_mutex);
					
					// Reporting the client
					sprintf(msg, "DOK\n");
					if(ts_sendall(socket, msg, strlen(msg)) < 0) service_exit(socket, remote_addr, ERROR_NOSEND);
				} else {	// If the entry was not found, ts_can is NULL
					pthread_mutex_unlock(&selling);
					
					// Reporting the log
					pthread_mutex_lock(&log_mutex);
					fprintf(log_fd, "FDE %s:%d:%d\n", inet_ntoa(remote_addr.sin_addr), remote_addr.sin_port, id);
					pthread_mutex_unlock(&log_mutex);
					
					sprintf(msg, "NOD\n");	
					if(ts_sendall(socket, msg, strlen(msg)) < 0) service_exit(socket, remote_addr, ERROR_NOSEND);
				} // MUTEX UNLOCKED either ways
			} else { // Bad id/pass format
				service_exit(socket, remote_addr, ERROR_FORMAT);
			}
			break;
		default: // A command is recognized, but its not valid for clients
			service_exit(socket, remote_addr, ERROR_FORMAT);
		}
	}
}

// MAIN
int main(int argc, char *argv[]) {
	FILE *dump_fd;
	pthread_t thread;								// Thread identifier
	pthread_attr_t thread_attr;						// Thread attributes
	struct sockaddr_in local_addr;					// Internet address structure
	struct timeval recv_to;							// Timeout for the recv() call
	int ser_socket, *arg_socket;					// Incoming, Service and Argument sockets
	int nt;											// Number of tickets in each gig-category pair
	int i, j, yes = 1;
	clients = NULL;
	
	// WRONG USAGE
	if(argc != 4) {
		printf("Usage: %s gigs categories tickets\n", argv[0]);
		return 1;
	}
	
	// We need to ignore the SIGPIPE signals, otherwise a broken client could kill the server
	if(signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
		perror("Failed to install the SIGPIPE handler");
		return 2;
	}
	
	// We end the execution of the server with a SIGINT signal
	if(signal(SIGINT, close_mainsocket) == SIG_ERR) {
		perror("Failed to install the SIGUSR1 handler");
		return 3;
	}
	if(signal(SIGTERM, close_mainsocket) == SIG_ERR) {
		perror("Failed to install the SIGUSR1 handler");
		return 3;
	}
	
	
	// PARAMETER PARSING AND MEMORY ALLOCATION/INITIALIZATION OF TICKETS, MUTEXES AND PTHREAD ATTR
	// Given a gig 'g' and a category 'c' the number of available tickets will be tickets[g][c]
	// Mutex protection is restricted to g-c pairs, so only an element in the matrix is blocked
	// in order to improve the performance
	ng = atoi(argv[1]);
	nc = atoi(argv[2]);
	nt = atoi(argv[3]);
	tickets = (int **)malloc(ng*sizeof(int *));
	for(i = 0; i < ng; i++) {
		tickets[i] = (int *)malloc(nc*sizeof(int));
		for(j = 0; j < nc; j++) tickets[i][j] = nt;
	}
	pthread_mutex_init(&selling, NULL);
	pthread_mutex_init(&log_mutex, NULL);
	pthread_attr_init(&thread_attr);
	pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED);
	
	// recv() timeout
	recv_to.tv_usec = 0;
	recv_to.tv_sec = RECVTO;
	
	// SYSTEM LOG
	if(LOGFILE) {
		umask(S_IWGRP | S_IWOTH);
		log_fd = fopen(LOGFILE, "w");
		if(log_fd == NULL) {
			perror("fopen");
			return 4;
		}
	} else log_fd = stderr;
	
	// SERVER SOCKET SETUP
	if((inc_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("Socket creation failed");
		return 5;
	}
	
	if(setsockopt(inc_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
	    perror("Setsockopt");
		return 6;
	}
	
	local_addr.sin_family = AF_INET;
	local_addr.sin_port = htons(PORT);
	local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	memset(&(local_addr.sin_zero), '\0', 8);
	
	if(bind(inc_socket, (struct sockaddr *)&local_addr, sizeof(struct sockaddr)) == -1) {
		perror("Binding failed");
		return 7;
	}
	
	// 128 is the size of the queue of clients waiting for the accept().
	if(listen(inc_socket, 128) == -1) {
		perror("Listen failed");
		return 8;
	}
	
	// MAIN LOOP, BEGINING OF THE MULTITHREADING
	while(1) {
		ser_socket = accept(inc_socket, NULL, NULL);
		if(ser_socket < 0) {
			if(errno == EBADF) {
				pthread_mutex_lock(&log_mutex);
				fprintf(stderr, "Execution finished. Dumping reservations...\n");
				pthread_mutex_unlock(&log_mutex);
				
				umask(S_IWGRP | S_IWOTH);
				dump_fd = fopen(DUMPFILE, "w");
				if(dump_fd == NULL) {
					perror("fopen");
					return 10;
				}
				
				ts_cdb_dump(clients, dump_fd);
				
				return 0;
			}
			else {
				perror("Accept");
				return 9;
			}
		}
		
		// We apply some timeout, so we can detect broken (or too slow) clients
		setsockopt(ser_socket, SOL_SOCKET, SO_RCVTIMEO, (void *)&recv_to, sizeof(struct timeval));
		
		// Ok, not so nice, but in some conditions, if we pass ser_socket, the main loop could
		// overwrite the socket before the thread reads it, so lets be cautious.
		// The thread is who has to free the memory when he is done
		arg_socket = (int *)malloc(sizeof(int));
		*arg_socket = ser_socket;
		
		// The threads are created detached, so we don't worry to join them
		pthread_create(&thread, NULL, service, (void *)arg_socket);
	}
	
	return 0;
}
