CC=clang-8
CFLAGS=-g -std=c99 -Wall -pedantic -Werror -fsanitize=address 
CWD=$(shell pwd)

all:
	$(CC) -Wl,-rpath $(CWD)/lib64 -L$(CWD)/lib64 -lruntime $(CFLAGS) *.c -o lab

clean:
	rm lab events.log pipes.log
