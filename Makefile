CC = gcc
CFLAGS = -fPIC -Wall -pthread -g

.PHONY: build
build: libscheduler.so

libscheduler.so: so_scheduler.o
	$(CC) -shared -pthread -g -o $@ $^
so_scheduler.o: _test/so_scheduler.c
	$(CC) $(CFLAGS) -o $@ -c $<

.PHONY: clean
clean:
	-rm -f so_scheduler.o libscheduler.so
