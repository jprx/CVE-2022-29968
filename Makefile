.PHONY: all
all: spray exploit fake_bio.o

fake_bio.o: fake_bio.c
	gcc fake_bio.c -c -o fake_bio.o -static

exploit: exploit.c fake_bio.o io_uring.h
	gcc fake_bio.o exploit.c -o exploit -static -lpthread

spray: spray.c
	gcc spray.c -o spray -static -lpthread
