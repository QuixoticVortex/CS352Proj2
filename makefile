all: main

main: lib-sfs.o ComS352Project2.o 
	gcc -o main ComS352Project2.o lib-sfs.o -lpthread

lib-sfs.o: lib-sfs.h lib-sfs.c
	gcc -c lib-sfs.c

ComS352Project2.o: lib-sfs.h ComS352Project2.c
	gcc -c ComS352Project2.c

clean:
	rm *.o main
