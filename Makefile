CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=c11
TARGETS = myprogram create_test_file

.PHONY: all clean

all: $(TARGETS)

myprogram: myprogram.c
	$(CC) $(CFLAGS) -o $@ $<

create_test_file: create_test_file.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGETS) fileA fileB fileC fileD fileA.gz fileB.gz result.txt