COMPILER = clang

CFLAGS = -Wall -Wextra -Wfloat-equal -Wundef -Wshadow -Wunreachable-code -Wpointer-arith -Wcast-align -Wstrict-prototypes -Wwrite-strings -Wswitch-default -Wswitch-enum -Winit-self

all: copy

copy: copy.c
	${COMPILER} ${CFLAGS} copy.c -o copy.exe

clean:
	rm -rf *.o
