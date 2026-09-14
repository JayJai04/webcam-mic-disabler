CC ?= cc
CFLAGS ?= -O2 -Wall

all: v4l2test audiocap

v4l2test: v4l2test.c
	$(CC) $(CFLAGS) -o $@ $<

audiocap: audiocap.c
	$(CC) $(CFLAGS) -o $@ $< -ldl

clean:
	rm -f v4l2test audiocap

.PHONY: all clean
