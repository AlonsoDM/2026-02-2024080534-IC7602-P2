CFLAGS = -std=gnu17 -Wall -Wextra -Werror -O2 -pthread
LDLIBS = -lcurl -lcjson -lssl -lcrypto

zonal-cache:
	mkdir -p build
	gcc $(CFLAGS) -o build/zonal-cache *.c $(LDLIBS)

test:
	mkdir -p build
	gcc $(CFLAGS) -I. -o build/pruebas $(filter-out main.c,$(wildcard *.c)) tests/*.c $(LDLIBS) -lcriterion
	./build/pruebas

.PHONY: zonal-cache test
