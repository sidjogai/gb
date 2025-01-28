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
};

static u32 palettes[][4] = {
        {0xFF9BBC0F, 0xFF8BAC0F, 0xFF306230, 0xFF0F380F}, /* green */
        {0xFFFFFFFF, 0xFFB6B6B6, 0xFF676767, 0xFF000000}, /* gray */
};
