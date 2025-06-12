CC = gcc
CFLAGS = -Wall
LDFLAGS = -lcurl -pthread -lmicrohttpd

OBJECTS = main.o metrics.o config.o alarm.o socket_client.o logger.o http_client.o

all: system_manager

system_manager: $(OBJECTS)
	$(CC) -o system_manager $(OBJECTS) $(LDFLAGS)

main.o: main.c metrics.h config.h alarm.h socket_client.h http_client.h logger.h
	$(CC) $(CFLAGS) -c main.c

metrics.o: metrics.c metrics.h logger.h
	$(CC) $(CFLAGS) -c metrics.c

config.o: config.c config.h logger.h
	$(CC) $(CFLAGS) -c config.c

alarm.o: alarm.c alarm.h http_client.h logger.h
	$(CC) $(CFLAGS) -c alarm.c

socket_client.o: socket_client.c socket_client.h logger.h
	$(CC) $(CFLAGS) -c socket_client.c

logger.o: logger.c logger.h
	$(CC) $(CFLAGS) -c logger.c

http_client.o: http_client.c http_client.h
	$(CC) $(CFLAGS) -c http_client.c

clean: