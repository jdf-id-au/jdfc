# ugh Apple https://stackoverflow.com/questions/64126942/malloc-nano-zone-abandoned-due-to-inability-to-preallocate-reserved-vm-space
# https://nullprogram.com/blog/2023/04/29/
# -g3 debug level 3

CFLAGS=-std=c17 -g3 \
-pedantic -Wall -Wextra \
-fPIC -fsanitize=address,undefined

jdf.o: jdf.c
	cc $(CFLAGS) -c $^ -o $@

libjdf.a: jdf.o
	ar -rcs $@ $^

test: test.c
	cc $(CFLAGS) -L. -ljdf $^ -o $@
	MallocNanoZone='0' ./test
