CC     = cc
SRC    = main.c
CFLAGS = -O3 -Wall
SDL    = $$(pkg-config --cflags --libs sdl3)
OUT    = gb

all:
	$(CC) $(SRC) -std=c11 $(CFLAGS) $(SDL) -o $(OUT)

clean:
	rm -f $(OUT)

.PHONY: all clean
