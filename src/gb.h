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

        u8  bootrom_disabled;   /* FF50 */
        u8 *bootrom;

        u8 *interrupt_enable; /* CPU's interrupt enable */
        u8 *interrupt_flag; /* CPU's interrupt flag */

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

        int  ram_size;  /* external ram size / 1 KiB */
        u8 *rom;
        u8 *external_ram;

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
        int  cycles_since_tima_overflow;

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

        u8 ime; /* IME: interrupt master enable flag [write only] */
        u8 interrupt_enable; /* FFFF - IE: Interrupt enable */
        u8 interrupt_flag; /* FF0F — IF: Interrupt flag */

        bool last_instr_was_ei;
        bool halted;
        bool halt_bug;
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

        u32  palette[4];        /* light to dark; ARGB8888 format */
        u32 *display_buf;       /* user-facing display; ARGB8888 format */

        /* PCNT is only incremented if shift_count has reached SCX & 7.
           Pushing pixels to LCD is only done when pixel_count >= 8. */

        u8   shift_count;       /* fifo shift counter (0 - 7) */
        u8   pixel_count;       /* pixel counter (0 - 167) */

        bool scx_pixels_dropped; /* whether initial scx % 8 pixels dropped */

        bool initial_fetch_completed; /* whether initial B01 completed */

        bool prev_stat_line;

        /* bit 7 of LCDC is set after being unset */
        bool lcd_reenabled;

        struct fetcher {
                u8 tile_id;
                u8 bitplane0;
                u8 bitplane1;
                fetcher fn;
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
        } bg_fifo, obj_fifo;

        int dots_since_scanline_started;
        int dots_since_frame_started; /* only for assert */
        int frame; /* only for assert */

        bool oam_accessible;
        bool vram_accessible;

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
        } obj_buf[10];
        int obj_buf_len;

        bool obj_encountered;
        int  obj_index;         /* index into obj_buf for current OBJ */

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

static void draw_tilemap_0x9C00(struct ppu *ppu, u32 buf[]);
static void draw_tilemap_0x9800(struct ppu *ppu, u32 buf[]);
static void draw_tile_data(struct ppu *, u32 buf[]);
static void draw_info(int fps, u32 *buf, int w, int h, u32 *palette, struct gameboy *gb);
