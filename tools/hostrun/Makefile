# Desktop test harness for the shared game core (Linux/macOS/MSYS2 gcc).
ROOT := ../..
CC ?= gcc
CFLAGS := -O2 -g -w -DSMW_NO_SDL -I$(ROOT)/port -I$(ROOT)/src -I$(ROOT)
SRCS := hostrun.c $(ROOT)/port/smw_port.c \
        $(wildcard $(ROOT)/src/*.c) $(wildcard $(ROOT)/src/snes/*.c)

hostrun: $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $@ -lm

clean:
	rm -f hostrun
