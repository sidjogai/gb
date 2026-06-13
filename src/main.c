#include <errno.h>
#include <stdalign.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#if SDL_BYTEORDER != SDL_LIL_ENDIAN
#error "System unsupported"
#endif

typedef int8_t   i8;
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define len(a) ((int)(sizeof(a) / sizeof(*a)))

#define assert(expr) SDL_assert(expr)

#define die(...)							\
	do {								\
		SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__); \
		exit(EXIT_FAILURE);					\
	} while (0)

#include "config.h"
#include "gb.h"

#include "ppu.c"
#include "cpu.c"
#include "debug.c"
#include "init.c"
#include "gui.c"
#include "mem.c"
#include "mbc.c"
#include "timer.c"
#include "joypad.c"

/* frame buffers */
static u32 gb_buf[160 * 144];
static u32 bg_map_buf[256 * 256];
static u32 window_map_buf[256 * 256];
static u32 sprites_buf[(8 * 10) * (8 * 4)];
static u32 tile_data_buf[(8 * 16 + 15) * (8 * 24 + 23)];
static u32 info_buf[(8 * 20) *  (8 * 8)];

static u8 rom_buf[8 * 1024 * 1024]; /* size of largest ROM */
static u8 external_ram[128 * 1024]; /* max external RAM */

static struct gameboy gb;

enum window_id { GB, BG_MAP, WINDOW_MAP, INFO, TILE_DATA, SPRITES };

static struct window windows[] = {
	[GB] = {
		.title  = "gb",
		.width  = 160,
		.height = 144,
		.scale  = 2,
		.buf    = gb_buf,
		.shown  = true,
	},
	[BG_MAP] = {
		.title  = "gb — background map",
		.width  = 256,
		.height = 256,
		.scale  = 1,
		.buf    = bg_map_buf,
	},
	[WINDOW_MAP] = {
		.title  = "gb — window map",
		.width  = 256,
		.height = 256,
		.scale  = 1,
		.buf    = window_map_buf,
	},
	[INFO] = {
		.title  = "gb — info",
		.width  = 8 * 20,
		.height = 8 * 8,
		.scale  = 2,
		.buf    = info_buf,
	},
	[SPRITES] = {
		.title  = "gb — sprites",
		.width  = 8 * 10,
		.height = 8 * 4,
		.scale  = 3,
		.buf    = sprites_buf,
	},
	[TILE_DATA] = {
		.title  = "gb — tile data",
		.width  = 16 * 8 + 15,
		.height = 24 * 8 + 23,
		.scale  = 2,
		.buf    = tile_data_buf,
	},
};

int main(int argc, char *argv[])
{
	char *bootrom        = NULL;
	char *rom            = NULL;
	bool  limit_fps      = true;
	u32  *palette        = palettes;
	int   target_fps     = 60;
	bool  save_requested = false;

	(void)bootrom;

	/* TODO: don't use getopt() */
	for (int opt; (opt = getopt(argc, argv, "b:p:s:dF")) != -1; )
		switch (opt)  {
		case 'b':
			bootrom = optarg;
			break;
		case 'd':
			for (int i = 0; i < len(windows); i++)
				windows[i].shown = true;
			break;
		case 'p':
			if (optarg[0] >= '1' && optarg[0] <= '9')
				if (optarg[0] - '0' <= len(palettes))
					palette += (optarg[0] - '1') * 4;
			break;
		case 'F':
			limit_fps = false;
			break;
		}

	if (optind > argc)
		die("no rom supplied");
	rom = argv[optind];

	if (!SDL_Init(SDL_INIT_VIDEO))
		sdl_fail();

	/* TODO(sdl3): joystick support */

	for (int i = 0; i < len(windows); i++)
		if (windows[i].shown)
			init_window(&windows[i]);

	u64 elapsed;

	init_gb(&gb, gb_buf, palette, external_ram, rom_buf);

	skip_bootrom(&gb);

	load_rom(&gb, rom);

	u64 target_duration = 1000000000 / target_fps;

	for (;;) {
		u64 start = clock_ns();

		SDL_Event event;
		if (SDL_PollEvent(&event)) {
			switch (event.type) {
			case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
				goto quit;
			case SDL_EVENT_KEY_DOWN:
				switch (event.key.key) {
				case KEY_A:
					gb.joypad.state[A] = true;
					break;
				case KEY_B:
					gb.joypad.state[B] = true;
					break;
				case KEY_START:
					gb.joypad.state[START] = true;
					break;
				case KEY_SELECT:
					gb.joypad.state[SELECT] = true;
					break;
				case KEY_LEFT:
					gb.joypad.state[LEFT] = true;
					break;
				case KEY_RIGHT:
					gb.joypad.state[RIGHT] = true;
					break;
				case KEY_DOWN:
					gb.joypad.state[DOWN] = true;
					break;
				case KEY_UP:
					gb.joypad.state[UP] = true;
					break;
				case QUIT:
					goto quit;
				case TOGGLE_WINDOW_MAP_WINDOW:
					toggle_window_shown(&windows[WINDOW_MAP]);
					break;
				case TOGGLE_SPRITES_WINDOW:
					toggle_window_shown(&windows[SPRITES]);
					break;
				case TOGGLE_BG_MAP_WINDOW:
					toggle_window_shown(&windows[BG_MAP]);
					break;
				case TOGGLE_TILE_DATA_WINDOW:
					toggle_window_shown(&windows[TILE_DATA]);
					break;
				case INCREASE_SCALE:
					for (int i = 0; i < len(windows); i++) {
						if (window_focused(&windows[i])) {
							int s = windows[i].scale;
							if (s < 9)
								s++;
							rescale_window(&windows[i], s);
						}
					}
					break;
				case DECREASE_SCALE:
					for (int i = 0; i < len(windows); i++) {
						if (window_focused(&windows[i])) {
							int s = windows[i].scale;
							if (s > 0)
								s--;
							rescale_window(&windows[i], s);
						}
					}
					break;
				case CYCLE_PALETTE:
					if (palette - palettes < len(palettes) - 4)
						palette += 4;
					else
						palette = palettes;
					memcpy(gb.ppu.palette, palette, sizeof gb.ppu.palette);
					break;
				}
				break;
			case SDL_EVENT_KEY_UP:
				switch (event.key.key) {
				case KEY_A:
					gb.joypad.state[A] = false;
					break;
				case KEY_B:
					gb.joypad.state[B] = false;
					break;
				case KEY_START:
					gb.joypad.state[START] = false;
					break;
				case KEY_SELECT:
					gb.joypad.state[SELECT] = false;
					break;
				case KEY_LEFT:
					gb.joypad.state[LEFT] = false;
					break;
				case KEY_RIGHT:
					gb.joypad.state[RIGHT] = false;
					break;
				case KEY_DOWN:
					gb.joypad.state[DOWN] = false;
					break;
				case KEY_UP:
					gb.joypad.state[UP] = false;
					break;
				}
				break;
			}
		}

		int cur_frame = gb.ppu.frame;
		while(gb.ppu.frame == cur_frame)
			step_cpu(&gb.cpu);

		if (save_requested) {
			save_state(&gb, external_ram, rom);
			save_requested = false;
		}

		if (windows[TILE_DATA].shown)
			draw_tile_data(&gb.ppu, windows[TILE_DATA].buf);

		if (windows[BG_MAP].shown)
			draw_bg_map(&gb.ppu, windows[BG_MAP].buf);

		if (windows[WINDOW_MAP].shown)
			draw_bg_map(&gb.ppu, windows[BG_MAP].buf);

		if (windows[SPRITES].shown)
			draw_sprites(&gb.ppu, windows[SPRITES].buf, palette);

		if (windows[INFO].shown && gb.ppu.frame % 10 == 0)
			draw_info(1,
				  windows[INFO].buf,
				  windows[INFO].width,
				  windows[INFO].height,
				  palette, &gb);

		for (int i = 0; i < len(windows); i++)
			if (windows[i].shown)
				render_window(&windows[i]);

		if (limit_fps && (elapsed = clock_ns() - start) < target_duration)
			sleep_ns(target_duration - elapsed);

		static char buf[20] = {0};
		u64 end = clock_ns();
		int fps = 1000000000 / (end - start);
		assert(end - start);
		snprintf(buf, 20, "gb - %d fps", fps);
		SDL_SetWindowTitle(windows[GB].window, buf);
	}

 quit:
	return 0;
}
