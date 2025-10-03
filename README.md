# jdf/jdfc

Experimental [Wellons-inspired](https://nullprogram.com/blog/2023/10/08/) trip back to the future. Uses [arenas](https://nullprogram.com/blog/2023/09/27/).

Includes:
- [multithreaded http server](jdfhttp.h) using libev, pthreads and lock-free concurrent queues
- [date arithmetic functions](jdfdate.h)
- [enhanced enum](make_constants.c) code generation from json using jansson
- basic argparser
