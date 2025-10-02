CC = riscv64-unknown-linux-gnu-gcc
DUMP = riscv64-unknown-linux-gnu-objdump
CFLAGS := -O3 -static -DRISCV

#CC = gcc
#CFLAGS := -O3 -std=gnu99

trace-submit: trace-submit.c tacit.h
	${CC} ${CFLAGS} -o trace-submit trace-submit.c

hello: hello.c 
	${CC} ${CFLAGS} -o hello hello.c

dump: ${TARGET}
	${DUMP} -D ${TARGET} > ${TARGET}.dump

clean:
	rm -f hello trace-submit
