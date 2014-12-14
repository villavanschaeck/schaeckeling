CC=gcc
CFLAGS=-Wall -g -std=c99
LDFLAGS=-lpthread -lftdi -lrt
APP=dmxmain

all: $(APP) dmxdog

$(APP): dmxd.o dmxdriver.o input.o colors.o mididriver.o nanokontroldriver.o net.o usbmididriver.o
	$(CC) -o $(APP) dmxd.o dmxdriver.o input.o colors.o mididriver.o nanokontroldriver.o net.o usbmididriver.o $(LDFLAGS)

dmxdriver.o: dmxdriver.c dmxdriver.h
	$(CC) -c $(CFLAGS) dmxdriver.c

dmxd.o: dmxd.c dmxd.h input.o dmxdriver.h
	$(CC) -c $(CFLAGS) dmxd.c

input.o: input.c dmxd.h dmxdriver.h
	$(CC) -c $(CFLAGS) input.c

colors.o: colors.c
	$(CC) -c $(CFLAGS) colors.c

net.o: net.c
	$(CC) -c $(CFLAGS) net.c

mididriver.o: mididriver.c mididriver.h
	$(CC) -c $(CFLAGS) mididriver.c

usbmididriver.o: usbmididriver.c usbmididriver.h
	$(CC) -c $(CFLAGS) usbmididriver.c

nanokontroldriver.o: nanokontroldriver.c mididriver.h
	$(CC) -c $(CFLAGS) nanokontroldriver.c

dmxdog: dmxdog.c
	cc -o dmxdog $(CFLAGS) dmxdog.c

clean:
	rm -f $(APP) dmxdog *.o
