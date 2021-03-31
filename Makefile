SRC=ds-subtitler.c
APPS=$(SRC:%.c=%)
CFLAGS=-O2 -ggdb -pipe -Wall
CC=gcc

LIBS_DS=$(shell pkg-config --cflags --libs sndfile libresample rnnoise) -lresample -ldeepspeech -lm

.PHONY: all clean

all: ds-subtitler

%: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(LIBS_DS)

clean:
	rm -f $(APPS)
