CC	= gcc
CFLAGS	= -g -Wall -pedantic-errors
SRCS	= $(wildcard *.c)
INCS	= $(wildcard *.h)
SRC_DIR	= src
INC_DIR	= include

all: bibakBOXServer bibakBOXClient

bibakBOXServer: $(SRC_DIR)/bibakBOXServer.c $(INC_DIR)/common.h
	$(CC) $(CFLAGS) -o $@ $<

bibakBOXClient: $(SRC_DIR)/bibakBOXClient.c $(INC_DIR)/common.h
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f bibakBOXServer bibakBOXClient