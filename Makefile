.PHONY: all clean nuke

TARG=f7disk

OFILES=\
	main.o\
	version.o\
	f7part.o\
	ptable.o\

all: o.$(TARG)

clean:
	@rm -vf $(OFILES)

nuke: clean
	@rm -vf o.$(TARG)

o.$(TARG): $(OFILES)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

CFLAGS=-Wall -Wextra -pedantic
