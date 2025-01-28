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

#define do_for_gb_keymap(code)                                  \
        code(KEY_A,      gb.joypad.state[A] = key_down)         \
        code(KEY_B,      gb.joypad.state[B] = key_down)         \
        code(KEY_START,  gb.joypad.state[START] = key_down)     \
        code(KEY_SELECT, gb.joypad.state[SELECT] = key_down)    \
        code(KEY_LEFT,   gb.joypad.state[LEFT] = key_down)      \
        code(KEY_RIGHT,  gb.joypad.state[RIGHT] = key_down)     \
        code(KEY_DOWN,   gb.joypad.state[DOWN] = key_down)      \
        code(KEY_UP,     gb.joypad.state[UP] = key_down)

#define do_for_misc_keymap(code)                                \
        code(QUIT, goto quit)                                   \
        code(RESET, goto reset)                                 \
             code(LOAD, load_state(&gb, external_ram, rom);)    \
        code(SAVE, save_requested = true;)                      \
        code(TARGET_UNCAPPED_SPEED, limit_fps = false;)         \
        code(TARGET_1X_SPEED,                                   \
             target_fps = 60;                                   \
             target_duration = 1000000000 / target_fps;)        \
        code(TARGET_2X_SPEED, target_fps = 120;                 \
             target_duration = 1000000000 / target_fps;)        \
        code(TARGET_3X_SPEED,                                   \
             target_fps = 180;                                  \
             target_duration = 1000000000 / target_fps;)        \
        code(TARGET_4X_SPEED,                                   \
             target_fps = 240;                                  \
             target_duration = 1000000000 / target_fps;)        \
        code(TARGET_5X_SPEED,                                   \
             target_fps = 300;                                  \
             target_duration = 1000000000 / target_fps;)        \
        code(TOGGLE_WINDOW_MAP_WINDOW,                          \
             toggle_window_shown(&windows[WINDOW_MAP]))         \
        code(TOGGLE_SPRITES_WINDOW,                             \
             toggle_window_shown(&windows[SPRITES]))            \
        code(TOGGLE_BG_MAP_WINDOW,                              \
             toggle_window_shown(&windows[BG_MAP]))             \
        code(TOGGLE_TILE_DATA_WINDOW,                           \
             toggle_window_shown(&windows[TILE_DATA]))          \
        code(INCREASE_SCALE,                                    \
             for (int i = 0; i < len(windows); i++) {           \
                     if (window_focused(&windows[i])) {         \
                             int s = windows[i].scale;          \
                             if (s < 9)                         \
                                     s++;                       \
                             rescale_window(&windows[i], s);    \
                     }                                          \
             })                                                 \
        code(DECREASE_SCALE,                                    \
             for (int i = 0; i < len(windows); i++) {           \
                     if (window_focused(&windows[i])) {         \
                             int s = windows[i].scale;          \
                             if (s < 9)                         \
                                     s++;                       \
                             rescale_window(&windows[i], s);    \
                     }                                          \
             })

#define handle_gb_key_down(key, action) case key: key_down = true; action; break;
#define handle_gb_key_up(key, action) case key: key_down = false; action; break;

/* enforce a delay to avoid toggling a window, saving the state, etc. many times
   per second as keypresses register over multiple frames */
u64 last_misc_keypress;
#define handle_misc_key_down(key, action) case key:    \
        if (frame + 5 > last_misc_keypress) {          \
                last_misc_keypress = frame; action;    \
        }                                              \
        break;                                         \

int main(int argc, char *argv[])
{
        char *bootrom    = NULL;
        char *rom        = NULL;
        bool  limit_fps  = true;
        u32  *palette    = palettes[0];
        int   target_fps = 60;

        bool save_requested = false;

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

 reset:
        init_gb(&gb, gb_buf, palette, external_ram, rom_buf);

        skip_bootrom(&gb);

        load_rom(&gb, rom);
        frame = 1;

        u64 target_duration = 1000000000 / target_fps;

        for (;;) {
                u64 start = clock_ns();

                bool key_down;
                SDL_Event event;
                if (SDL_PollEvent(&event)) {
                        switch (event.type) {
                        case SDL_KEYDOWN:
                                switch (event.key.keysym.sym) {
                                        do_for_gb_keymap(handle_gb_key_down);
                                        do_for_misc_keymap(handle_misc_key_down);
                                }
                                break;
                        case SDL_KEYUP:
                                switch (event.key.keysym.sym) {
                                        do_for_gb_keymap(handle_gb_key_up);
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

                if (windows[INFO].shown && frame % 10 == 0)
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
