CC	= gcc
CFLAGS	= -g -Wall -pedantic-errors -lpthread -lrt
SRCS	= $(wildcard *.c)
INCS	= $(wildcard *.h)
SRC_DIR	= src

all: main

main: $(SRC_DIR)/main.c 
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f main	