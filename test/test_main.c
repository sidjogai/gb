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
static u64 *FRAME;

#define SUCCESS_CODE 11
#define FAILURE_CODE 13
#define TIMEOUT_CODE 15
#define ASSERT_FAILURE_CODE 15

#define PPU_LOGGING_ENABLED 0

#define LOG_PPU_LYC_WRITE   0
#define LOG_PPU_BGP_WRITE   0
#define LOG_PPU_STAT_WRITE  0
#define LOG_PPU_LCDC_WRITE  0
#define LOG_PPU_VBLANK_IRQ  0
#define LOG_PPU_STAT_IRQ    0
#define LOG_PPU_MODE_SWITCH 0
#define LOG_PPU_OAM_ACCESS  0
#define LOG_PPU_VRAM_ACCESS 0
#define LOG_PPU_LCD_TOGGLE  0

#define TRACE_CPU 0

#define LOG_CPU_EI_DI 0

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

struct gameboy gb;

static void test_mooneye(struct cpu *cpu)
{
        u64 frame = 1;
        FRAME = &frame;

        for (u64 n = 0; n < 1000000 ; n++) {
                step_cpu(&gb.cpu);
                /* jr -2 */
                bool inf_loop = read_mem(cpu->mem, cpu->regs.pc) == 0x18&&
                        read_mem(cpu->mem, cpu->regs.pc + 1) == 0xFE;

                if (inf_loop) {
                        if (cpu->regs.b == 3 && cpu->regs.c == 5 &&
                            cpu->regs.d == 8 && cpu->regs.e == 13 &&
                            cpu->regs.h == 21 && cpu->regs.l == 34)
                                exit(SUCCESS_CODE);
                        else
                                exit(FAILURE_CODE);
                }
        }
        exit(3);
}

int main(int argc, char *argv[])
{
        char *rom = argv[1];

        init_gb(&gb, gb_buf, palette, external_ram, rom_buf);
        TICK = &gb.cpu.tick;

        skip_bootrom(&gb);

        load_rom(&gb, rom);

        if (argc > 2 && strcmp(argv[2], "--mooneye") == 0)
                test_mooneye(&gb.cpu);

        return 0;
}
