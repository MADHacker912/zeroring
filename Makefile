CC = gcc
CFLAGS = -Wall -Wextra -O3 -Iinclude -fPIC
LDFLAGS = -luring

SRCDIR = src
INCDIR = include
BUILDDIR = build
BINDIR = bin

TARGET_LIB = $(BUILDDIR)/libzeroring.a
TEST_BIN = $(BINDIR)/zeroring_test
BENCH_BIN = $(BINDIR)/zeroring_bench

.PHONY: all clean test bench

all: $(TARGET_LIB) $(TEST_BIN) $(BENCH_BIN)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BINDIR):
	mkdir -p $(BINDIR)

$(BUILDDIR)/zeroring.o: $(SRCDIR)/zeroring.c $(INCDIR)/zeroring.h | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET_LIB): $(BUILDDIR)/zeroring.o
	ar rcs $@ $^

$(TEST_BIN): tests/test_engine.c $(TARGET_LIB) | $(BINDIR)
	$(CC) $(CFLAGS) $< -L$(BUILDDIR) -lzeroring $(LDFLAGS) -o $@

$(BENCH_BIN): bench/bench_engine.c $(TARGET_LIB) | $(BINDIR)
	$(CC) $(CFLAGS) $< -L$(BUILDDIR) -lzeroring $(LDFLAGS) -o $@

test: $(TEST_BIN)
	./$(TEST_BIN)

bench: $(BENCH_BIN)
	./$(BENCH_BIN)

clean:
	rm -rf $(BUILDDIR) $(BINDIR)
