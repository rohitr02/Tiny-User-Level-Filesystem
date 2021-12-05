CC = gcc
CFLAGS = -g

all: simple_test test_case bitmap_check

simple_test:
	$(CC) $(CFLAGS) -o simple_test simple_test.c

test_case:
	$(CC) $(CFLAGS) -o test_case test_case.c

bitmap_check:
	$(CC) $(CFLAGS) -o bitmap_check bitmap_check.c

clean:
	rm -rf simple_test test_case bitmap_check
