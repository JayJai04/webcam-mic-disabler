CC ?= cc
CFLAGS ?= -O2 -Wall

all: v4l2test audiocap

v4l2test: v4l2test.c
	$(CC) $(CFLAGS) -o $@ $<

audiocap: audiocap.c
	$(CC) $(CFLAGS) -o $@ $< -ldl

clean:
	rm -f v4l2test audiocap

.PHONY: all clean install

install: all
	install -d $(DESTDIR)/usr/local/bin
	install -m 755 v4l2test audiocap $(DESTDIR)/usr/local/bin/

uninstall:
	rm -f $(DESTDIR)/usr/local/bin/v4l2test $(DESTDIR)/usr/local/bin/audiocap
