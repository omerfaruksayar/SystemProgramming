CC	= gcc
CFLAGS	= -g -Wall -pedantic-errors -lpthread -lrt
SRCS	= $(wildcard *.c)
INCS	= $(wildcard *.h)
SRC_DIR	= src
INC_DIR	= include

all: biboServer biboClient

biboServer: $(SRC_DIR)/biboServer.c $(INC_DIR)/common.h
	$(CC) $(CFLAGS) -o $@ $<

biboClient: $(SRC_DIR)/biboClient.c $(INC_DIR)/common.h
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f biboServer biboClient	