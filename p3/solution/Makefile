CC = gcc
# Asan and Valgrind conflict:
# 1. Asan
# CFLAGS-common = -Wall -Wextra -Werror -pedantic -std=gnu18 --sanitize=address -fno-omit-frame-pointer
# 2. Valgrind
CFLAGS-common = -Wall -Wextra -Werror -pedantic -std=gnu18 -fno-omit-frame-pointer
CFLAGS = $(CFLAGS-common) -O2
CFLAGS-dbg = $(CFLAGS-common) -g -Og -ggdb
# LDFLAGS = -static-libasan
SRC = wsh.c
DEPS = wsh.h
OBJS = wsh.o
TARGET = wsh
LOGIN = m0mosenpai
SUBMITPATH = /usr/home/~cs537-1/handin/${LOGIN}

all: ${TARGET} ${TARGET}-dbg

%.o: %.c %.h
	$(CC) $< -c $(CFLAGS)

$(TARGET): $(SRC) ${OBJS}
	$(CC) $< -o $@ ${CFLAGS}

$(TARGET)-dbg: $(SRC) ${OBJS}
	$(CC) $< -o $@ ${CFLAGS-dbg}

clean:
	rm -f $(TARGET) $(TARGET)-dbg ${OBJS}

# to-do:
test:
	make clean
	cd tests

submit:
	cp -r ../../p3 $(SUBMITPATH)
	cd $(SUBMITPATH)/p3
	make clean

.PHONY: all clean
