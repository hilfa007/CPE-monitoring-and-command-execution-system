CC = gcc
CFLAGS = -Wall

all: device_agent test_day3

device_agent: device_agent.c
	$(CC) $(CFLAGS) -o device_agent device_agent.c

test_day3: test_day3.c
	$(CC) $(CFLAGS) -o test_day3 test_day3.c

clean:
	rm -f device_agent test_day3 metrics.log /tmp/device_agent.sock
