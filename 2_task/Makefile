CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=c11 -D_POSIX_C_SOURCE=200809L
TARGETS = myprogram

.PHONY: all clean

all: $(TARGETS)

myprogram: myprogram.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGETS) *.lck stats.txt sharedfile