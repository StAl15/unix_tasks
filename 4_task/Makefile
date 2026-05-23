# Makefile для броуновского бота.
# Кроссплатформенный (Linux / macOS). Сборка двух исполняемых файлов:
#   bin/server, bin/client, bin/gen_numbers

CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -D_POSIX_C_SOURCE=200809L
LDFLAGS ?=

UNAME := $(shell uname)
ifeq ($(UNAME), Darwin)
    # На macOS sbrk убран из публичных заголовков — нужен _DARWIN_C_SOURCE.
    CFLAGS += -D_DARWIN_C_SOURCE
endif
ifeq ($(UNAME), Linux)
    # На glibc < 2.17 clock_gettime находится в отдельной librt.
    LDFLAGS += -lrt
endif

SRCDIR  := src
SCRDIR  := scripts
BINDIR  := bin

SERVER_SRC  := $(SRCDIR)/server.c
CLIENT_SRC  := $(SRCDIR)/client.c
COMMON_HDR  := $(SRCDIR)/common.h
GENNUM_SRC  := $(SCRDIR)/gen_numbers.c
METRICS_SRC := $(SCRDIR)/calc_metrics.c

SERVER_BIN  := $(BINDIR)/server
CLIENT_BIN  := $(BINDIR)/client
GENNUM_BIN  := $(BINDIR)/gen_numbers
METRICS_BIN := $(BINDIR)/calc_metrics

.PHONY: all clean

all: $(SERVER_BIN) $(CLIENT_BIN) $(GENNUM_BIN) $(METRICS_BIN)

$(BINDIR):
	mkdir -p $(BINDIR)

$(SERVER_BIN): $(SERVER_SRC) $(COMMON_HDR) | $(BINDIR)
	$(CC) $(CFLAGS) $(SERVER_SRC) -o $@ $(LDFLAGS)

$(CLIENT_BIN): $(CLIENT_SRC) $(COMMON_HDR) | $(BINDIR)
	$(CC) $(CFLAGS) $(CLIENT_SRC) -o $@ $(LDFLAGS)

$(GENNUM_BIN): $(GENNUM_SRC) | $(BINDIR)
	$(CC) $(CFLAGS) $(GENNUM_SRC) -o $@ $(LDFLAGS)

$(METRICS_BIN): $(METRICS_SRC) | $(BINDIR)
	$(CC) $(CFLAGS) $(METRICS_SRC) -o $@ $(LDFLAGS) -lm

clean:
	rm -rf $(BINDIR)
