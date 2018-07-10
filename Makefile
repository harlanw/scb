build: bin bin/scb

bin:
	mkdir bin

bin/scb: demo.c scb/scb.c
	$(CC) $^ -o $@ -Wall -Wextra -Wno-unused-result -pedantic -std=c99 -O4

.PHONY: build
