CC      = gcc
CFLAGS  = -Wall -Wextra -pedantic -std=c99 -D_POSIX_C_SOURCE=200809L
TARGET  = myinit

.PHONY: all clean

all: $(TARGET)

$(TARGET): myinit.c
	$(CC) $(CFLAGS) -o $(TARGET) myinit.c

clean:
	rm -f $(TARGET)
