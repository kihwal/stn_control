all: mtune

cal.o: cal.c
	gcc -c cal.c -o cal.o
tdata.o: tdata.c
	gcc -c tdata.c -o tdata.o
mtune: mtune.c cal.o tdata.o
	gcc -o mtune mtune.c cal.o tdata.o -lncurses -lm -lhamlib -lcurl
clean:
	rm mtune cal.o tdata.o
