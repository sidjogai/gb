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
        DIV_ADDR        = 0xFF04,
        TIMA_ADDR       = 0xFF05,
        TMA_ADDR        = 0xFF06,
        TAC_ADDR        = 0xFF07,
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
        WY_ADDR         = 0xFF4A,
        WX_ADDR         = 0xFF4B,
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

static void write_mem(struct mem *, u8, u16);
static u8 read_mem(struct mem *, u16);

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


static u8   read_external_ram(struct mbc *, u16);
static u8   read_rom(struct mbc *, u16);
static void write_external_ram(struct mbc *, u8, u16);
static void write_rom(struct mbc *, u8, u16);

/* ================================ timer.c ================================= */

struct timer {
        u16 div;  /* FF04 - DIV: divider register (upper byte mapped) */
        u8  tima; /* FF05 - TIMA: timer counter */
        u8  tma;  /* FF06 - TMA: timer modulo */
        u8  tac;  /* FF07 - TAC: timer control */

        bool prev_and_result;   /* last result of DIV counter & timer_enabled */
        bool tima_overflowed;   /* reset when TIMA is reloaded */
        bool tima_reloaded;     /* whether TIMA was reloaded this cycle */
        int  cycles_since_tima_overflow; /* m cycles */

        u8 *interrupt_flag;
};

static void sync_timer(struct timer *, u8 *interrupt_flag);
static void write_timer(struct timer *, u8, u16);
static u8   read_timer(struct timer *, u16);

/* ================================= cpu.c ================================== */

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
        u8 op; /* last fetched op code */

        /* struct ime_with_tick { */
        /*         u64  tick; */
        /*         bool enabled; */
        /* } ime; */
        u8 ime; /* IME: interrupt master enable flag [write only] */

        bool last_instr_was_ei;
        bool is_paused; /* halt */
};

static void tick_cpu(struct cpu *);

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
        u8 dma;                 /* FF46 — DMA: OAM DMA source address & start */
        u8 bgp;                 /* FF47 - BG palette data */
        u8 obp0;                /* FF48 - OBJ palette 0 */
        u8 obp1;                /* FF49 - OBJ palette 1 */
        u8 wy;                  /* FF4A - Window Y position */
        u8 wx;                  /* FF4B - Window X position plus 7 */

        u8 lx;                  /* internal horizontal counter; not exposed */

        u8   cycles_since_dma_requested;
        u8   dma_offset;
        bool dma_in_progress;
        bool dma_requested;       /* set when DMA (FF46) written to */
        bool oam_access_blocked;

        u32  palette[4];        /* light to dark; ARGB8888 format */
        u32 *display_buf;       /* user-facing display; ARGB8888 format */

        u8   shift_count;       /* fifo shift counter (0 - 7) */
        u8   pixel_count;       /* pixel counter (0 - 167) */

        /* shift count only incremented after first B01 */
        bool shift_counter_enabled;
        /* pixel count only incremented if shift count has reached SCX & 7 */
        bool pixel_counter_enabled;

        bool prev_stat_line;

        /* ppu is disabled if bit 7 of LCDC is set */
        bool disabled;

        struct fetcher {
                u8 tile_id;
                u8 bitplane0;
                u8 bitplane1;
                enum fetcher_state {
                        FETCH_TILE_ID_IDLE,
                        FETCH_TILE_ID,
                        FETCH_BITPLANE0_IDLE,
                        FETCH_BITPLANE0,
                        FETCH_BITPLANE1_IDLE,
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

        struct mem *mem; /* required for OAM DMA */
};

static void sync_ppu(struct ppu *, u8 *interrupt_flag);

static void write_vram(struct ppu *, u8, u16);
static u8   read_vram(struct ppu *, u16);

static void write_oam(struct ppu *, u8, u16);
static u8   read_oam(struct ppu *, u16);

static u8   read_ppu_reg(struct ppu *, u16);
static void write_ppu_reg(struct ppu *, u8 v, u16);

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
