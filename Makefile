.PHONY = run
# ugh Apple https://stackoverflow.com/questions/64126942/malloc-nano-zone-abandoned-due-to-inability-to-preallocate-reserved-vm-space
# https://nullprogram.com/blog/2023/04/29/
# -g3 debug level 3

CFLAGS=-std=c17 -g3 \
-pedantic -Wall -Wextra \
-fPIC -fsanitize=address,undefined

run: testrel
	MallocNanoZone='0' time ./testrel

test: test.c
	time cc $(CFLAGS) $^ -o $@

testrel: testrel.c
	time cc $(CFLAGS) $^ -o $@

testhttp: testhttp.c
	time cc $(CFLAGS) -lev -lpthread $^ -o $@
