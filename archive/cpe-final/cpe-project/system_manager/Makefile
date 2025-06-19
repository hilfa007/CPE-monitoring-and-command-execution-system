CC=gcc
CFLAGS=-Wall
LDFLAGS=-lcurl

all: system_manager

system_manager: main.o metrics.o config.o alarm.o device_agent_client.o logger.o http_client.o
	$(CC) -o system_manager $^ $(LDFLAGS)

main.o: main.c
	$(CC) $(CFLAGS) -c main.c

metrics.o: metrics.c
	$(CC) $(CFLAGS) -c metrics.c

config.o: config.c
	$(CC) $(CFLAGS) -c config.c

alarm.o: alarm.c
	$(CC) $(CFLAGS) -c alarm.c

device_agent_client.o: device_agent_client.c
	$(CC) $(CFLAGS) -c device_agent_client.c

logger.o: logger.c
	$(CC) $(CFLAGS) -c logger.c

http_client.o: http_client.c
	$(CC) $(CFLAGS) -c http_client.c

clean:
	rm -f *.o system_manager