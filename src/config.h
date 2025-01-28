enum keymap {
        KEY_B                    = 'z',
        KEY_A                    = 'x',
        KEY_SELECT               = SDLK_RETURN,
        KEY_START                = SDLK_BACKSPACE,

        KEY_RIGHT                = SDLK_RIGHT,
        KEY_LEFT                 = SDLK_LEFT,
        KEY_UP                   = SDLK_UP,
        KEY_DOWN                 = SDLK_DOWN,

        QUIT                     = SDLK_ESCAPE,
        RESET                    = 'r',

        TOGGLE_BG_MAP_WINDOW     = 'b',
        TOGGLE_INFO_WINDOW       = 'i',
        TOGGLE_TILE_DATA_WINDOW  = 't',
        TOGGLE_WINDOW_MAP_WINDOW = 'w',
        TOGGLE_SPRITES_WINDOW    = 's',

        DECREASE_SCALE           = '[',
        INCREASE_SCALE           = ']',
};

static u32 palettes[][4] = {
        {0xFFE0F8D0, 0xFF88C070, 0xFF346856, 0xFF081820}, /* green (light) */
        {0xFF9BBC0F, 0xFF8BAC0F, 0xFF306230, 0xFF0F380F}, /* green */
        {0xFFFFFFFF, 0xFFB6B6B6, 0xFF676767, 0xFF000000}, /* gray */
};

/* ============================= Debug options ============================== */

#define PPU_LOGGING_ENABLED 1 /* Whether any PPU logging is enabled */

#define LOG_PPU_LYC_WRITE   1 /* LYC register written to */
#define LOG_PPU_BGP_WRITE   1 /* BGP register written to */
#define LOG_PPU_STAT_WRITE  1 /* STAT register written to */
#define LOG_PPU_LCDC_WRITE  1 /* LCDC register written to */
#define LOG_PPU_SCX_WRITE   1 /* SCX register written to */
#define LOG_PPU_VBLANK_IRQ  1 /* VBlank interrupt requested */
#define LOG_PPU_STAT_IRQ    1 /* STAT interrupt requested */
#define LOG_PPU_MODE_SWITCH 0 /* PPU mode switched */
#define LOG_PPU_OAM_ACCESS  0 /* OAM read/write access changed */
#define LOG_PPU_VRAM_ACCESS 0 /* VRAM read/write access changed */
#define LOG_PPU_LCD_TOGGLE  1 /* LCD turned on or off */

#define TRACE_CPU 0 /* Log CPU state after each instruction */

#define LOG_CPU_EI_DI 0 /* Interrupts enabled or disabled */
