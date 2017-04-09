CC=/opt/llvm-3.9.1/bin/clang
CFLAGS=-g -std=c99 -Wall -pedantic -Werror

all:
	$(CC) -Wl,-rpath ./lib64/libruntime.so -lruntime $(CFLAGS) *.c -o lab

clean:
	rm lab events.log pipes.log
