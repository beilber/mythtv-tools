prefix = /usr

CC = gcc

all : scanfw

scanfw : CFLAGS = -Wall -g
scanfw : LDFLAGS = -g
scanfw : LDLIBS =  -lraw1394 -liec61883 -lavc1394 -lrom1394 -lpthread

scanfw : scanfw.o scanprime.o changers.o

clean :
	rm -rf  *.o  

install :
	install scanfw $(prefix)/bin/scanfw

.PHONY: clean install
