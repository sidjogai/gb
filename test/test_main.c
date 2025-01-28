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

#define Q assert(false);

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

#define SUCCESS_CODE 11
#define FAILURE_CODE 13
#define ASSERT_FAILURE_CODE 15

#define len(a) ((int)(sizeof(a) / sizeof(*a)))

#define assert(expr) do { if(!(expr)) exit(ASSERT_FAILURE_CODE); } while (0)

#define die(...)                                                            \
        do {                                                                \
                SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__); \
                exit(EXIT_FAILURE);                                         \
        } while (0)

#include "../src/gb.h"


#include "../src/ppu.c"
#include "../src/cpu.c"
#include "../src/debug.c"
#include "../src/init.c"
#include "../src/gui.c"
#include "../src/mem.c"
#include "../src/mbc.c"
#include "../src/timer.c"
#include "../src/joypad.c"

static u32 gb_buf[160 * 144];

static u8 rom_buf[8 * 1024 * 1024]; /* size of the largest gameboy ROM */
static u8 external_ram[128 * 1024]; /* max external RAM */

static u32 palette[]  = {0xFFFFFFFF, 0xFFB6B6B6, 0xFF676767, 0xFF000000};

static void early_exit(struct cpu *cpu)
{
        bool inf_loop = read_mem(cpu->mem, cpu->regs.pc) == 0x18 /* jr */ &&
                read_mem(cpu->mem, cpu->regs.pc + 1) == 0xFE; /* -2 */

        if (inf_loop) {
                if (cpu->regs.b == 3 && cpu->regs.c == 5 &&
                    cpu->regs.d == 8 && cpu->regs.e == 13 &&
                    cpu->regs.h == 21 && cpu->regs.l == 34)
                        exit(SUCCESS_CODE);
                else
                        exit(FAILURE_CODE);
        }
}

int main(int argc, char *argv[])
{
        char *rom = argv[1];

        struct gameboy gb;
        init_gb(&gb, gb_buf, palette, external_ram, rom_buf);
        TICK = &gb.cpu.tick;

        skip_bootrom(&gb);

        load_rom(&gb, rom);

        if (argc > 2 && strcmp(argv[2], "-q") == 0) {
                for (;;) {
                        /* for (int i = 0; i < (70224 / 4); i++) */
                                step_cpu(&gb.cpu);
                        early_exit(&gb.cpu);
                }
        }

        struct window gb_window = {
                .title  = "gb",
                .width  = 160,
                .height = 144,
                .scale  = 2,
                .buf    = gb_buf,
                .shown = true,
        };

        if (SDL_Init(SDL_INIT_VIDEO) < 0)
                sdl_fail();
        atexit(SDL_Quit);

        init_window(&gb_window);



        u64 frame;

        frame = 1;

#ifdef GB_PROFILE
        u64 total_ns = 0;
#endif

        for (SDL_Event event; ; frame++) {
                if (SDL_PollEvent(&event)) {
                        switch (event.type) {
                        case SDL_KEYDOWN:
                                switch (event.key.keysym.sym) {
                                case SDLK_ESCAPE:
                                        exit(FAILURE_CODE);
                                case SDLK_SPACE:;

#ifdef GB_PROFILE
                                        u64 avg_ns = total_ns / frame;
                                        u64 avg_fps = 1000000000 / avg_ns;

                                        printf("frames = %llu, avg fps = %llu\n",
                                               frame, avg_fps);
#endif
                                        exit(SUCCESS_CODE);
                                }
                        }
                }

#ifdef GB_PROFILE
                u64 frame_start = clock_ns();
#endif

                while(gb.cpu.tick <= frame * 70224)
                        step_cpu(&gb.cpu);

#ifdef GB_PROFILE
                u64 delta = clock_ns() - frame_start;
                total_ns += delta;
                double ms = delta / 1E6;
                u64 fps = 1000000000 / delta;
                printf("frame %llu - %f ms (= %llu fps)\n", frame, ms, fps);
#endif

                render_window(&gb_window);
        }
        return 0;
}
