LDFLAGS=`pkg-config fuse openssl sqlite3 --libs`

dedupfs : dedupfs.o log.o database.o
	gcc -g -o dedupfs dedupfs.o log.o database.o $(LDFLAGS)

dedupfs.o : dedupfs.c database.h log.h params.h
	gcc -g -Wall `pkg-config fuse --cflags` -c dedupfs.c

database.o : database.c database.h log.h params.h
	gcc -g -Wall `pkg-config fuse --cflags` -c database.c

bbfs : bbfs.o log.o
	gcc -g -o bbfs bbfs.o log.o `pkg-config fuse --libs`

bbfs.o :  bbfs.c log.h params.h
	gcc -g -Wall `pkg-config fuse --cflags` -c bbfs.c

log.o : log.c log.h params.h
	gcc -g -Wall `pkg-config fuse --cflags` -c log.c

clean:
	rm -f dedupfs *.o
