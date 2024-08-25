.PHONY: test

test: example
	MallocNanoZone='0' ./example

example: example.c jdf.c
	cc -std=c17 -g3 -pedantic -Wall -Wextra -fPIC -fsanitize=address,undefined $^ -o $@
