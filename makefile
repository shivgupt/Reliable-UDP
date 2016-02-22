++ = g++
FLAGS = -lpthread

all: udpserver udpclient clean

udpserver: udpserver.o 
	$(++) -o $@ $^ $(FLAGS)

udpclient: udpclient.o
	$(++) -o $@ $^

clean:
	rm *.o
