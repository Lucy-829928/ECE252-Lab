CC=gcc
CFLAGS=-Wall -g
LD=gcc
LDFLAGS=-g

OBJS1=findpng.o
OBJS2=catpng.o

all: findpng.out catpng.out

findpng.out: $(OBJS1)
	$(CC) $(CFLAGS) -o $@ $(OBJS1)

catpng.out: $(OBJS2)
	$(CC) $(CFLAGS) -o $@ $(OBJS2)

.c.o:
	$(CC) $(CFLAGS) -c $<

%.d: %.c
	gcc -MM -MF $@ $<

-include $(OBJS1:.o=.d) $(OBJS2:.o=.d)

.PHONY: clean
clean:
	rm -f *.d *.o *.out