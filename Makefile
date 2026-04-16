CC=gcc
CFLAGS=`pkg-config fuse3 --cflags`
LIBS=`pkg-config fuse3 --libs`

all:
	$(CC) mini_unionfs.c -o mini_unionfs $(CFLAGS) $(LIBS)

clean:
	rm -f mini_unionfs
