CC	= gcc
CFLAGS	= -g -Wall -pedantic-errors
SRC_DIR	= src

all: appendMeMore dups

appendMeMore: $(SRC_DIR)/appendMeMore.c
	$(CC) $(CFLAGS) $(SRC_DIR)/appendMeMore.c -o appendMeMore

dups: $(SRC_DIR)/dups.c
	$(CC) $(CFLAGS) $(SRC_DIR)/dups.c -o dups

clean:
	rm -rf appendMeMore dups test.txt