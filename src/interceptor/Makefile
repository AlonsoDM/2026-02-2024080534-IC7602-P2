CC = gcc
CFLAGS = -std=gnu17 -Wall -Wextra -Werror -O2 -pthread
LDLIBS = -lldns -lcurl -lcjson
SOURCES = $(wildcard *.c)

interceptor: build/interceptor

build/interceptor: $(SOURCES) $(wildcard *.h)
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $(SOURCES) $(LDLIBS)

build/pruebas: $(filter-out main.c,$(SOURCES)) $(wildcard *.h tests/*.c tests/*.h)
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -I. -o $@ $(filter-out main.c,$(SOURCES)) tests/*.c $(LDLIBS) -lcriterion

test: build/pruebas
	./build/pruebas --jobs 1

.PHONY: interceptor test
