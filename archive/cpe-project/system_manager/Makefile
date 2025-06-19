CC=gcc
CFLAGS=-Wall

all: system_manager

system_manager: main.o metrics.o config.o alarm.o socket_client.o logger.o http_client.o
	$(CC) -o system_manager $^ -lcurl

main.o: main.c
	$(CC) $(CFLAGS) -c main.c

metrics.o: metrics.c
	$(CC) $(CFLAGS) -c metrics.c

config.o: config.c
	$(CC) $(CFLAGS) -c config.c

alarm.o: alarm.c
	$(CC) $(CFLAGS) -c alarm.c

socket_client.o: socket_client.c
	$(CC) $(CFLAGS) -c socket_client.c

logger.o: logger.c
	$(CC) $(CFLAGS) -c logger.c

clean:
	rm -f *.o system_manager
