CC = riscv64-unknown-linux-gnu-gcc
DUMP = riscv64-unknown-linux-gnu-objdump
CFLAGS := -O3 -static -DRISCV

#CC = gcc
#CFLAGS := -O3 -std=gnu99

trace-hello: trace-hello.c tacit.h
	${CC} ${CFLAGS} -o trace-hello trace-hello.c

dump: trace-hello
	${DUMP} -D trace-hello > trace-hello.dump

clean:
	rm -f trace-hello
