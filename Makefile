# ugh Apple https://stackoverflow.com/questions/64126942/malloc-nano-zone-abandoned-due-to-inability-to-preallocate-reserved-vm-space
# https://nullprogram.com/blog/2023/04/29/
# -g3 debug level 3

test: test.c jdf.c
	cc -std=c17 -g3 -pedantic -Wall -Wextra -fPIC \
		-fsanitize=address,undefined $^ -o $@
	MallocNanoZone='0' ./test

example: example.c jdf.c
	time cc -std=c17 -g3 -pedantic -Wall -Wextra -fPIC -fsanitize=address,undefined $^ -o $@
	MallocNanoZone='0' ./example
