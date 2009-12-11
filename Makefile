MACHINE = `uname -p`
FLAGS = -Wall -g

all: clientgen server
	
clientgen: clientgen.c common.o
	gcc clientgen.c common.o -o clientgen-${MACHINE} -lpthread ${FLAGS}
server: server.c common.o ts_server.o
	gcc server.c common.o ts_server.o -o server-${MACHINE} -lpthread ${FLAGS}
ts_server.o: ts_server.c
	gcc -c ts_server.c ${FLAGS}
common.o: common.c
	gcc -c common.c ${FLAGS}
clean:
	rm -f server-* clientgen-* *.o
clean-backup:
	rm -f *~
