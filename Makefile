CC=gcc
CFLAGS=-Wall -g 
LDFLAGS=-lreadline
BINARY=icsh

all: icsh

icsh: icsh.c
	$(CC) -o $(BINARY) $(CFLAGS) $< $(LDFLAGS)

.PHONY: clean

clean:
	rm -f $(BINARY)
