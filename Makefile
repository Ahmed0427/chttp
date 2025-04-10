CC = gcc
CFLAGS = -Wall -Wextra -pedantic -ggdb 

CFILES = main.c
EXEC = server

all:
	$(CC) $(CFLAGS) $(CFILES) -o $(EXEC)

run: all
	./$(EXEC) 8080

clean:
	rm $(EXEC)
