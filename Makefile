CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2

.PHONY: all demo clean
all: sandbox demo_program

sandbox: sandbox.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

demo_program: demo_program.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

demo: all
	sh demo.sh

clean:
	rm -f sandbox demo_program
