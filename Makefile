.PHONY = run
# ugh Apple https://stackoverflow.com/questions/64126942/malloc-nano-zone-abandoned-due-to-inability-to-preallocate-reserved-vm-space
# https://nullprogram.com/blog/2023/04/29/
# -g3 debug level 3
# ugh can't really include .h files as deps
# https://stackoverflow.com/a/43527114/780743

PREAMBLE=MallocNanoZone='0' time

CFLAGS=-std=c17 -g3 \
-pedantic -Wall -Wextra \
-fPIC -fsanitize=address,undefined

test: test.c
	time cc $(CFLAGS) $^ -o $@
	$(PREAMBLE) ./$@

testrel: testrel.c
	time cc $(CFLAGS) $^ -o $@
	$(PREAMBLE) ./$@

testhttp: testhttp.c
	time cc $(CFLAGS) -lev -lpthread $^ -o $@
	$(PREAMBLE) ./$@

make_constants: make_constants.c
	time cc $(CFLAGS) -ljansson $^ -o $@

http_codes.h: make_constants http_codes.json
	$(PREAMBLE) ./make_constants http_codes.json > http_codes.h
