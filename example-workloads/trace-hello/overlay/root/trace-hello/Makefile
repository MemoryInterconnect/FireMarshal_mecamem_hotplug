CC = riscv64-unknown-linux-gnu-gcc
CFLAGS := -O3 -static -DRISCV

#CC = gcc
#CFLAGS := -O3 -std=gnu99

trace-hello: trace-hello.c tacit.h
	${CC} ${CFLAGS} -o trace-hello trace-hello.c

clean:
	rm -f trace-hello
