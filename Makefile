CFLAGS = -g -Wall
ALL: dos_scandisk
dos_scandisk: dos_scandisk.o dos.o
	$(CC) $(CFLAGS) -o dos_scandisk dos_scandisk.o dos.o