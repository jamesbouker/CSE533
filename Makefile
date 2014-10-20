CC = gcc

LIBS = -lsocket\
	/home/courses/cse533/Stevens/unpv13e_solaris2.10/libunp.a
	
FLAGS = -g -O2 -Werror

CFLAGS = ${FLAGS} -I/home/courses/cse533/Stevens/unpv13e_solaris2.10/lib

PROGS =	get_ifi_info_plus.o server client

CLEANFILES = get_ifi_info_plus.o server.o client.o

all:	${PROGS}


server: 	server.o
			${CC} ${CFLAGS} -o $@ server.o get_ifi_info_plus.o ${LIBS}	

client: 	client.o
			${CC} ${CFLAGS} -o $@ client.o get_ifi_info_plus.o ${LIBS}			

clean:
		rm -f ${PROGS} ${CLEANFILES}
