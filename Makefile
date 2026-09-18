# Makefile for MinGW
#
# Prerequisites: mingw-w64, zlib (pacman -S mingw-w64-x86_64-zlib)
#
CC      = gcc
CFLAGS  = -O2 -std=c99 -Wall -I. -Ilib -Isrc
LDFLAGS = -luser32 -lgdi32 -lcomdlg32 -lcomctl32 -Wl,-Bstatic -lz -Wl,-Bdynamic

SRCS = src/main.c src/qr_window.c lib/qrcodegen.c
OBJS = $(SRCS:.c=.o)
RES  = resource.res
EXE  = qifi-win32.exe

all: $(EXE)

$(EXE): $(OBJS) $(RES)
	$(CC) -mwindows -o $@ $(OBJS) $(RES) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

resource.res: resource.rc resource.h
	windres resource.rc resource.res

clean:
	rm -f $(OBJS) $(RES) $(EXE)

.PHONY: all clean
