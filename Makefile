# ugh Apple https://stackoverflow.com/questions/64126942/malloc-nano-zone-abandoned-due-to-inability-to-preallocate-reserved-vm-space
# https://nullprogram.com/blog/2023/04/29/
# -g3 debug level 3
# https://stackoverflow.com/a/43527114/780743

PREAMBLE=MallocNanoZone='0' time
# https://www.gnu.org/software/make/manual/html_node/Automatic-Variables.html
CFLAGS=-std=c17 -g3 \
-pedantic -Wall -Wextra \
-fPIC -fsanitize=address,undefined

test: test.c jdf.h
	time cc $(CFLAGS) $< -o $@
	$(PREAMBLE) ./$@

test_rel: test_rel.c relptr.h 
	time cc $(CFLAGS) $< -o $@
	$(PREAMBLE) ./$@

test_http: test_http.c jdfhttp.h http_codes.h
	time cc $(CFLAGS) -lev -lpthread $< -o $@
	du -sh $@
	$(PREAMBLE) ./$@

test_date: test_date.c jdfdate.h dates.h
	time cc $(CFLAGS) $< -o $@
	$(PREAMBLE) ./$@

make_constants: make_constants.c
	time cc $(CFLAGS) -ljansson $< -o $@

dates.h: make_constants dates.json
	$(PREAMBLE) ./$< dates.json > $@

http_codes.h: make_constants http_codes.json
	$(PREAMBLE) ./$< http_codes.json > $@
