prefix = /usr

CC = gcc

all : mythprime

mythprime : CFLAGS = -Wall -g
mythprime : LDFLAGS = -g
mythprime : LDLIBS =  -lraw1394 -liec61883 -lavc1394 -lrom1394 

mythprime : mythprime.o

clean :
	rm -rf  *.o  

install :
	install mythprime $(prefix)/bin/mythprime

.PHONY: clean install
