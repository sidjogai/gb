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

#include "SDL.h"

#if SDL_BYTEORDER != SDL_LIL_ENDIAN
#error "System unsupported"
#endif

#ifdef GB_DEBUG
#define Q assert(false);
#else
#define Q ;
#endif

typedef int8_t   i8;
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef u64      tick;
typedef int      m_cycle;
typedef int      t_cycle;
typedef int      dot;

static u64 *TICK;
static u64 *FRAME;

#define len(a) ((int)(sizeof(a) / sizeof(*a)))

#define assert(expr) SDL_assert(expr)

#define die(...)                                                            \
        do {                                                                \
                SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__); \
                exit(EXIT_FAILURE);                                         \
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

static u8 rom_buf[8 * 1024 * 1024]; /* size of largest gameboy ROM */
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
                .shown = true,
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

static void handle_keypress(int keypress)
{
        switch(keypress) {
        case TOGGLE_WINDOW_MAP_WINDOW:
                toggle_window_shown(&windows[WINDOW_MAP]);
                break;
        case TOGGLE_BG_MAP_WINDOW:
                toggle_window_shown(&windows[BG_MAP]);
                break;
        case TOGGLE_INFO_WINDOW:
                toggle_window_shown(&windows[INFO]);
                break;
        case TOGGLE_TILE_DATA_WINDOW:
                toggle_window_shown(&windows[TILE_DATA]);
                break;
        case TOGGLE_SPRITES_WINDOW:
                toggle_window_shown(&windows[SPRITES]);
                break;

        case KEY_A:
                gb.joypad.state[A] = true;
                break;
        case KEY_B:
                gb.joypad.state[B] = true;
                break;
        case KEY_SELECT:
                gb.joypad.state[SELECT] = true;
                break;
        case KEY_START:
                gb.joypad.state[START] = true;
                break;

        case KEY_RIGHT:
                gb.joypad.state[RIGHT] = true;
                break;
        case KEY_LEFT:
                gb.joypad.state[LEFT] = true;
                break;
        case KEY_UP:
                gb.joypad.state[UP] = true;
                break;
        case KEY_DOWN:
                gb.joypad.state[DOWN] = true;
                break;

        case INCREASE_SCALE:
        case DECREASE_SCALE:
                for (int i = 0; i < len(windows); i++) {
                        if (window_focused(&windows[i])) {
                                int s = windows[i].scale;
                                if (keypress == INCREASE_SCALE && s < 9)
                                        s++;
                                else if (s > 1)
                                        s--;
                                rescale_window(&windows[i], s);
                        }
                }
                break;
        }
}

int main(int argc, char *argv[])
{
        char *bootrom               = NULL;
        char *rom                   = NULL;
        bool  limit_fps             = true;
        u32  *palette               = palettes[0];

        (void)bootrom;

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

        if (SDL_Init(SDL_INIT_VIDEO) < 0)
                sdl_fail();

        for (int i = 0; i < len(windows); i++)
                if (windows[i].shown)
                        init_window(&windows[i]);

        u64 frame, start, elapsed;
        FRAME = &frame;

 start:
        init_gb(&gb, gb_buf, palette, external_ram, rom_buf);
        TICK = &gb.cpu.tick;

        skip_bootrom(&gb);

        load_rom(&gb, rom);
        frame = 1;

        for (SDL_Event event; ; frame++) {
                start = clock_ns();

                memset(gb.joypad.state, 0, sizeof gb.joypad.state);

                if (SDL_PollEvent(&event)) {
                        switch (event.type) {
                        case SDL_KEYDOWN:;
                                int sym = event.key.keysym.sym;
                                switch (sym) {
                                case QUIT:
                                        goto done;
                                case RESET:
                                        goto start;
                                default:
                                        handle_keypress(sym);
                                        break;
                                }
                        }
                }

                u64 gb_t0 = clock_ns();

                while(gb.cpu.tick <= frame * (70224 / 4))
                        step_cpu(&gb.cpu);

                int gb_fps = 1000000000 / (clock_ns() - gb_t0);

                if (windows[TILE_DATA].shown)
                        draw_tile_data(&gb.ppu, windows[TILE_DATA].buf);

                if (windows[BG_MAP].shown)
                        draw_tilemap_0x9800(&gb.ppu, windows[BG_MAP].buf);

                if (windows[WINDOW_MAP].shown)
                        draw_tilemap_0x9C00(&gb.ppu, windows[WINDOW_MAP].buf);

                if (windows[SPRITES].shown)
                        draw_sprites(&gb.ppu, windows[SPRITES].buf, palette);

                if (windows[INFO].shown && frame % 10 == 0)
                        draw_info(gb_fps,
                                  windows[INFO].buf,
                                  windows[INFO].width,
                                  windows[INFO].height,
                                  palette, &gb);

                for (int i = 0; i < len(windows); i++)
                        if (windows[i].shown)
                                render_window(&windows[i]);

                if (limit_fps && (elapsed = clock_ns() - start) < 16666667)
                        sleep_ns(16666667 - elapsed);
        }

 done:
        return 0;
}
