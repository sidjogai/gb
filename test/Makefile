CC     = cc
SRC    = test_main.c
CFLAGS = -O3
SDL    = $$(pkg-config --cflags --libs sdl3)
OUT    = gb-test

all:
	$(CC) $(SRC) -std=c11 $(CFLAGS) $(SDL) -o $(OUT)

clean:
	rm -f $(OUT)

.PHONY: all clean
