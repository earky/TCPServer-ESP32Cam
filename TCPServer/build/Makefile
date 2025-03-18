CXX = g++
CFLAGS = -std=c++14 -O2 -Wall -g 

TARGET = server
OBJS = ../code/log/*.cpp ../code/server/*.cpp \
       ../code/buffer/*.cpp ../code/main.cpp

MMAP = mmapTest errorMmap

all: $(OBJS)
	$(CXX) $(CFLAGS) $(OBJS) -o $(TARGET)  -pthread 

clean:
	rm -rf ../bin/$(OBJS) $(TARGET) $(MMAP)




