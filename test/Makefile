CC     = cc
SRC    = test_main.c
CFLAGS = -O3
SDL    = $$(sdl2-config --cflags --libs)
OUT    = gb-test
TAGS   = uctags -e --kinds-c=DLdefghlmstuvzp

all:
	$(CC) $(SRC) -std=c11 $(CFLAGS) $(SDL) -o $(OUT)

clean:
	rm -f $(OUT)

tags:
	$(TAGS) -R $$(sdl2-config --prefix)/include/SDL2 *.[ch]

.PHONY: all clean tags
