.SUFFIXES : .o .cpp
OBJS = tcpprox.o
SRCS = tcpprox.c
CFLAGS = -g
CC = gcc

tcpprox: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)
	
.cpp.o:
	$(CC) $(CFLAGS) -c $<
	
clean:
	rm -f *.o
	rm -f tcpprox
	rm -f *.core

depend:
	makedepend -- $(SRCS)
