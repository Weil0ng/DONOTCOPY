#This is the makefile to make the emulator
CC=gcc
LD=gcc

CFLAGS=-O2 -std=c99 -g
OBJS=DiskEmulator.o FileSystem.o SuperBlock.o Utility.o

Layer0Test: $(OBJS) Layer0Test.o
	$(CC) $(CFLAGS) -o $@ $^

Layer1DBlkTest: $(OBJS) Layer1DBlkTest.o
	$(CC) $(CFLAGS) -o $@ $^

Layer1Test: $(OBJS) Layer1INodeTest.o
	$(CC) $(CFLAGS) -o $@ $^
    
%.o: %.c %.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -fr Layer0Test Layer1DBlkTest Layer1INodeTest *.o
