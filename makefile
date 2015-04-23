all: main

main: lib-sfs.o main.o 
	gcc -o main main.o lib-sfs.o -lpthread

lib-sfs.o: lib-sfs.h lib-sfs.c
	gcc -c lib-sfs.c

main.o: lib-sfs.h ComS352Project2.c
	gcc -c ComS352Project2.c

clean:
	rm *.o main
