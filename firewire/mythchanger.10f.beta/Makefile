prefix = /usr

CC = gcc

all : mythchanger

mythchanger : CFLAGS = -Wall -g
mythchanger : LDFLAGS = -g
mythchanger : LDLIBS =  -lraw1394 -liec61883 -lavc1394 -lrom1394 

mythchanger : mythchanger.o

clean :
	rm -rf  *.o  

install :
	install mythchanger $(prefix)/bin/mythchanger

.PHONY: clean install
