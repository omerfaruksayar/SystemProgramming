CC	= gcc
CFLAGS	= -g -Wall -pedantic-errors
SRCS	= $(wildcard *.c)
INCS	= $(wildcard *.h)
SRC_DIR	= src
INC_DIR	= include

all: server client

server: $(SRC_DIR)/biboServer.c $(INC_DIR)/asQueue.h
	$(CC) $(CFLAGS) -o $@ $<

client: $(SRC_DIR)/biboClient.c $(INC_DIR)/asQueue.h
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f server client	