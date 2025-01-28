enum addr {
        ROM0_START      = 0x0000,
        ROM0_END        = 0x3FFF,
        TILE_DATA_START = 0x8000,
        VRAM_START      = 0x8000,
        TILE_DATA_END   = 0x97FF,
        TILEMAP1_START  = 0x9800,
        TILEMAP1_END    = 0x9BFF,
        TILEMAP2_START  = 0x9C00,
        TILEMAP2_END    = 0x9FFF,
        VRAM_END        = 0x9FFF,
        WRAM_START      = 0xC000,
        WRAM_END        = 0xDFFF,
        OAM_START       = 0xFE00,
        OAM_END         = 0xFE9F,
        IOREG_START     = 0xFF00,
        REG_DIV_ADDR    = 0xFF04,
        REG_TIMA_ADDR   = 0xFF05,
        REG_TMA_ADDR    = 0xFF06,
        REG_TAC_ADDR    = 0xFF07,
        REG_IF_ADDR     = 0xFF0F,
        LCDC_ADDR       = 0xFF40,
        STAT_ADDR       = 0xFF41,
        SCY_ADDR        = 0xFF42,
        SCX_ADDR        = 0xFF43,
        LY_ADDR         = 0xFF44,
        LYC_ADDR        = 0xFF45,
        DMA_ADDR        = 0xFF46,
        BGP_ADDR        = 0xFF47,
        OBP0_ADDR       = 0xFF48,
        OBP1_ADDR       = 0xFF49,
        IOREG_END       = 0xFF7F,
        HRAM_START      = 0xFF80,
        HRAM_END        = 0xFFFE,
        IE_REG          = 0xFFFF,
        REG_IE_ADDR     = 0xFFFF,
};

#define mem_size(x) (x##_END - x##_START + 1)

/* ================================= mem.c ================================== */

struct mem {
        u8   wram[mem_size(WRAM)];
        u8   hram[mem_size(HRAM)];
        u8   int_enable;        /* interrupt enable register (0xFFFF) */
        u8   int_flag;          /* interrupt flag (0xFF0F) */
        u8   tma;               /* timer modulo (0xFF06) */
        u16  div;
        bool prev_result;
        u8   tima;
        tick last_tima_write;
        bool is_timer_pending;
        tick timer_irq;
        u8   tac;               /* timer control (0xFF07) */

        u8  bootrom_disabled;   /* FF50 */
        u8 *bootrom;
        
        struct timer  *timer;
        struct ppu    *ppu;
        struct joypad *joypad;
        struct mbc    *mbc;

};

static void write_mem(struct mem *, u8 v, u16 addr, tick tick);
static u8   read_mem(struct mem *, u16 addr, tick tick);

/* ================================= mbc.c ================================== */

struct mbc {
        enum mbc_type { NO_MBC, MBC1 } type;
        int rom_banks;          /* number of 16KiB ROM banks */
        int ram_banks;          /* number of 8KiB RAM banks */
        
        /* int  rom_banks;    /\* number of 16KiB rom banks *\/ */
        int  ram_size;  /* external ram size / 1 KiB */
        u8 *rom;
        u8 *external_ram;

        /* MBC1 specific fields */


        struct mbc1 {
                u8 ram_enabled; /* set if 0xA written to 0000-1FFF */ 
                u8 bank1;       /* 2000-3FFF - BANK1: MBC1 bank register 1  */
                u8 bank2;       /* 4000-5FFF - BANK2: MBC1 bank register 2  */
                u8 mode;        /* 6000-7FFF - MODE: MBC1 mode register */
        } mbc1;
};


static u8 read_external_ram(struct mbc *mbc, u16 addr);
static u8 read_rom(struct mbc *mbc, u16 addr);
static void write_external_ram(struct mbc *mbc, u8 v, u16 addr);
static void write_rom(struct mbc *mbc, u8 v, u16 addr);

/* ================================ timer.c ================================= */

struct timer {
        struct {
                bool  is_pending;
                tick tick_due;
        } irq;
        bool prev_result;
        u16  div;
        u8   tac;
        u8   tima;
        u8   tma;
};

static void sync_timers(struct timer *, tick cur_tick, u8 *interrupt_flag);

static void write_div(struct timer *, u8);
static u8   read_div(struct timer *);

static void write_tima(struct timer *, u8, tick cur_tick);
static u8   read_tima(struct timer *);

static void write_tma(struct timer *, u8);
static u8   read_tma(struct timer *);

static void write_tac(struct timer *, u8);
static u8   read_tac(struct timer *);

/* ================================= cpu.c ================================== */

#ifdef GB_TRACE
/* https://github.com/wheremyfoodat/Gameboy-logs */
#define TRACE(...)                                                       \
        do {                                                             \
                struct registers r_ = cpu->regs;                         \
                printf("A: %.02X F: %.02X B: %.02X C: %.02X "            \
                       "D: %.02X E: %.02X H: %.02X L: %.02X "            \
                       "SP: %.04X PC: 00:%.04X "                         \
                       "(%.02X %.02X %.02X %.02X) ",                     \
                       r_.a, r_.f, r_.b, r_.c, r_.d,                     \
                       r_.e, r_.h, r_.l, r_.sp, r_.pc,                   \
                       read_mem(cpu->mem, cpu->regs.pc, cpu->tick),      \
                       read_mem(cpu->mem, cpu->regs.pc + 1, cpu->tick),  \
                       read_mem(cpu->mem, cpu->regs.pc + 2, cpu->tick),  \
                       read_mem(cpu->mem, cpu->regs.pc + 3, cpu->tick)); \
                printf(__VA_ARGS__);                                     \
                putchar('\n');                                           \
        } while(0)
#else
#define TRACE(...) ;
#endif

struct cpu {
        tick tick;
        struct mem *mem;
        struct registers {
                union { u16 af; struct { u8 f; u8 a; }; };
                union { u16 bc; struct { u8 c; u8 b; }; };
                union { u16 de; struct { u8 e; u8 d; }; };
                union { u16 hl; struct { u8 l; u8 h; }; };
                union { u16 pc; struct { u8 pc_lsb; u8 pc_msb; }; };
                union { u16 sp; struct { u8 sp_lsb; u8 sp_msb; }; };
        } regs;
        struct ime_with_tick {
                u64  tick;
                bool enabled;
        } ime;
        bool is_paused; /* halt */
};

static void tick_cpu(struct cpu *cpu);

/* ================================= ppu.c ================================== */

typedef void (*fetcher)(struct ppu *);

struct ppu {
        u8 vram[mem_size(VRAM)];
        u8 oam[mem_size(OAM)];

        u8 lcdc;                /* FF40 - LCDC: LCD control */
        u8 stat;                /* FF41 - STAT: LCD status */
        u8 scy;                 /* FF42 - SCY: background viewport Y position */
        u8 scx;                 /* FF43 - SCX: background viewport X position */
        u8 ly;                  /* FF44 - LY: LCD Y coordinate [read-only] */
        u8 lyc;                 /* FF45 - LYC: LY compare */
        u8 bgp;                 /* FF47 - BG palette data */
        u8 obp0;                /* FF48 - OBJ palette 0 */
        u8 obp1;                /* FF49 - OBJ palette 1 */
        u8 wy;                  /* FF4A - Window Y position */
        u8 wx;                  /* FF4B - Window X position plus 7 */

        u8 lx;                  /* internal horizontal counter; not exposed */

        u32  palette[4];        /* light to dark; ARGB8888 format */
        u32 *display_buf;       /* user-facing display; ARGB8888 format */

        struct new {
                int pixelcount;
                int nfetch; /* times a tile fetch occured; debugging only */
                bool obj_fetch_underway;
                struct obj *cur_obj; /* object for which tile fetch underway */
                int q;
        } new;

        struct fetcher {
                u8 tile_id;
                u8 bitplane0;
                u8 bitplane1;
                enum fetcher_state {
                        FETCH_TILE_ID,
                        FETCH_BITPLANE0,
                        FETCH_BITPLANE1,
                        PUSH,
                } state;
        } bg_fetcher, obj_fetcher;
        enum active_fetcher {BG_FETCHER, OBJ_FETCHER} active_fetcher;

        struct fifo {
                struct fifo_entry {
                        u8 color;       /* 0-3; palette not applied */
                        u8 palette;     /* bit 4 from OAM attributes */
                        u8 priority;    /* bit 7 from OAM attributes */
                } entries[8];
                int len;
                int head;
                /* bool active; /\* whether pixels are popped from fifo each dot *\/ */
        } bg_fifo, obj_fifo;

        /* static_assert(sizeof(struct fetcher) == 3); */

        struct obj *cur_obj;

        struct mem *mem; /* required for OAM DMA */
        struct dma {
                bool    in_progress;
                m_cycle delta;  /* cycles elapsed since OAM DMA initiated */
                u8      val;    /* FF46 — DMA: OAM DMA source address & start */
        } dma;

        t_cycle line_delta; /* dots elapsed since scan line started */


        enum ppu_mode {
                HBLANK,
                VBLANK,
                OAM_SCAN,
                DRAWING,
        } mode;

        struct obj {
                u8 y;
                u8 x;
                u8 tile_index;
                u8 attributes;
                bool seen;
        } obj_slots[10];
        int nslots;
};

static void sync_ppu(struct ppu *, tick cur_tick, u8 *interrupt_flag);

static void write_vram(struct ppu *, u8, u16 addr);
static u8   read_vram(struct ppu *, u16 addr);

static void write_oam(struct ppu *, u8, u16 addr);
static u8   read_oam(struct ppu *, u16 addr);

static void write_lcdc(struct ppu *, u8);
static u8   read_lcdc(struct ppu *);

static void write_stat(struct ppu *, u8);
static u8   read_stat(struct ppu *);

static void write_scy(struct ppu *, u8);
static u8   read_scy(struct ppu *);

static void write_scx(struct ppu *, u8);
static u8   read_scx(struct ppu *);

static void write_ly(struct ppu *, u8);
static u8   read_ly(struct ppu *);

static void write_lyc(struct ppu *, u8);
static u8   read_lyc(struct ppu *);

static void write_bgp(struct ppu *, u8);
static u8   read_bgp(struct ppu *);

static void write_dma(struct ppu *, u8, tick cur_tick);
static u8   read_dma(struct ppu *);

static void write_wy(struct ppu *, u8);
static u8   read_wy(struct ppu *);

static void write_wx(struct ppu *, u8);
static u8   read_wx(struct ppu *);

static u8 * get_tile(struct ppu *ppu, u8 i); /* debug.c */

/* ================================ joypad.c ================================ */

struct joypad {
        u8 p1; /* FF00 - P1: Joypad */

        /* written to from the main SDL loop */
        bool state[8];
};

enum key {
        A,
        B,
        START,
        SELECT,
        LEFT,
        RIGHT,
        DOWN,
        UP,
};

static void write_p1(struct joypad *, u8);
static u8   read_p1(struct joypad *);

/* ================================== gb.c ================================== */

struct gameboy {
        struct cpu    cpu;
        struct mem    mem;
        struct mbc    mbc;
        struct timer  timer;
        struct ppu    ppu;
        struct joypad joypad;

        u8 *rom;

        u8 bootrom[256];
};

static void init_gb(struct gameboy *gb,
                    u32 *display_buf,
                    u32 *palette,
                    u8 *external_ram_buf,
                    u8 *rom_buf);
static void load_rom(struct gameboy *gb, const char *filename);

/* ================================ debug.c ================================= */

static void draw_bg_map(struct ppu *, u32 buf[]);
static void draw_tile_data(struct ppu *, u32 buf[]);
/* static void draw_info(int fps, u32 *buf, int w, int h, u32 *palette); */
static void draw_info(int fps, u32 *buf, int w, int h, u32 *palette, struct gameboy *gb);

/* ================================= gui.c ================================== */

struct window {
        char *title;
        int   width;
        int   height;
        int   scale;
        u32  *buf;
        bool  shown;

        SDL_Renderer *renderer;
        SDL_Texture  *texture;
        SDL_Window   *window;
};

static noreturn void sdl_fail(void);

static void init_window(struct window *);
static void render_window(struct window *);
static void close_window(struct window *);
static void toggle_window_shown(struct window *);

static u64 clock_ns(void);
static void sleep_ns(u64 ns);
