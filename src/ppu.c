static void fetch_bg_tile_id_idle(struct ppu *ppu);
static void fetch_bg_tile_id(struct ppu *ppu);
static void fetch_bg_bitplane0_idle(struct ppu *ppu);
static void fetch_bg_bitplane0(struct ppu *ppu);
static void fetch_bg_bitplane1_idle(struct ppu *ppu);
static void fetch_bg_bitplane1(struct ppu *ppu);
static void bg_push(struct ppu *ppu);
static void fetch_obj_tile_id_idle(struct ppu *ppu);
static void fetch_obj_tile_id(struct ppu *ppu);
static void fetch_obj_bitplane0_idle(struct ppu *ppu);
static void fetch_obj_bitplane0(struct ppu *ppu);
static void fetch_obj_bitplane1_idle(struct ppu *ppu);
static void fetch_obj_bitplane1(struct ppu *ppu);

#define BIN_FMT "%c%c%c%c'%c%c%c%c"
#define BIN(x)                                                  \
        ('0' + ((x >> 7) & 0x1)), ('0' + ((x >> 6) & 0x1)),     \
        ('0' + ((x >> 5) & 0x1)), ('0' + ((x >> 4) & 0x1)),     \
        ('0' + ((x >> 3) & 0x1)), ('0' + ((x >> 2) & 0x1)),     \
        ('0' + ((x >> 1) & 0x1)), ('0' + ((x >> 0) & 0x1))

static bool do_logging;

#define log_event(event, ...)                                           \
        do {                                                            \
                if (PPU_LOGGING_ENABLED && LOG_PPU_##event) {           \
                        if (do_logging) {                               \
                                printf("frame = %3d, "                  \
                                       "mode = %d, "                    \
                                       "ly = %3d, "                     \
                                       "pcnt = %3d, "                   \
                                       "dot = %3d "                     \
                                       "dots-since-frame = %3d "        \
                                       "stat = "BIN_FMT" "              \
                                       "[%s]    \t",                    \
                                       ppu->frame,                      \
                                       ppu->mode,                       \
                                       ppu->ly,                         \
                                       ppu->pixel_count,                \
                                       ppu->dots_since_scanline_started, \
                                       ppu->dots_since_frame_started,   \
                                       BIN(ppu->stat),                  \
                                       #event);                         \
                                printf(__VA_ARGS__);                    \
                                putchar('\n');                          \
                        }                                               \
                }                                                       \
        } while (0)

enum lcdc_flag {
        BG_WIN_ENABLE = 1 << 0,
        OBJ_ENABLE    = 1 << 1,
        OBJ_SIZE      = 1 << 2,
        BG_NAMETABLE  = 1 << 3,
        LCD_ENABLE    = 1 << 7,
};

static void set_vram_access(struct ppu *ppu, bool enabled)
{
        ppu->vram_accessible = enabled;
        log_event(VRAM_ACCESS, "%s", enabled ? "enabled" : "disabled");
}

static void set_oam_access(struct ppu *ppu, bool enabled)
{
        ppu->oam_accessible = enabled;
        log_event(OAM_ACCESS, "%s", enabled ? "enabled" : "disabled");
}

static void update_coincidence_flag(struct ppu *ppu)
{
        if (read_ppu_reg(ppu, LY_ADDR) == ppu->lyc)
                ppu->stat |= (1 << 2);
        else
                ppu->stat &= ~(1 << 2);
}

static void switch_to_mode(struct ppu *ppu, enum ppu_mode m)
{
        log_event(MODE_SWITCH, "switched from mode %d to %d", ppu->mode, m);
        ppu->mode = m;
        ppu->stat &= ~0x7;
        ppu->stat |= ppu->mode;
        update_coincidence_flag(ppu);
}


static void oam_dma(struct ppu *ppu);

static void new_oam_scan(struct ppu *ppu);
static void new_drawing(struct ppu *ppu);
static void new_hblank(struct ppu *ppu, u8 *interrupt_flag);
static void new_vblank(struct ppu *ppu, u8 *interrupt_flag);

static bool check_stat(struct ppu *ppu, u8 *interrupt_flag);

static void tick_ppu(struct ppu *ppu, u8 *interrupt_flag)
{
        if (!(ppu->lcdc & LCD_ENABLE)) {
                ppu->ly = 0;
                ppu->stat &= ~0x3;
                ppu->dots_since_scanline_started = 0;
                ppu->dots_since_frame_started = 0;
                ppu->vram_accessible = true;
                ppu->oam_accessible = true;
                return;
        }

        switch (ppu->mode) {
        case OAM_SCAN:
                assert(ppu->dots_since_scanline_started < 80); /* unless weird one */
                new_oam_scan(ppu);
                break;
        case DRAWING:
                assert(ppu->dots_since_scanline_started >= 80);
                new_drawing(ppu);
                break;
        case HBLANK:
                new_hblank(ppu, interrupt_flag);
                break;
        case VBLANK:
                new_vblank(ppu, interrupt_flag);
                break;
        }

        /* printf("mode is %d dss is %d\n", ppu->mode, ppu->dots_since_scanline_started); */

        update_coincidence_flag(ppu);

        check_stat(ppu, interrupt_flag);

        ppu->dots_since_scanline_started++;
        ppu->dots_since_frame_started++;
}


static void init_new_scanline(struct ppu *ppu)
{
        /* set_oam_access(ppu, false); */
        assert(ppu->vram_accessible);

        ppu->active_fetcher = BG_FETCHER;
        ppu->obj_encountered = false;

        ppu->initial_fetch_completed = false;
        ppu->scx_pixels_dropped = false;

        ppu->bg_fifo.len   = 0;
        ppu->bg_fifo.head  = 0;
        ppu->obj_fifo.len  = 0;
        ppu->obj_fifo.head = 0;

        ppu->obj_buf_len = 0;

        ppu->bg_fetcher.fn  = fetch_bg_tile_id_idle;
        ppu->obj_fetcher.fn = fetch_obj_tile_id_idle;

        ppu->shift_count = 0;
        ppu->pixel_count = 0;

        /* as dots_since_scanline_started++ at the end of tick_ppu() */
        ppu->dots_since_scanline_started = -1;
}

static void new_hblank(struct ppu *ppu, u8 *interrupt_flag)
{
        /* TODO: weird ly stuff */
        if (ppu->dots_since_scanline_started == 455) {

                check_stat(ppu, interrupt_flag);
                update_coincidence_flag(ppu);
                check_stat(ppu, interrupt_flag);
                ppu->ly++;
                update_coincidence_flag(ppu);
                check_stat(ppu, interrupt_flag);
                init_new_scanline(ppu);

                if (ppu->ly == 144) {
                        switch_to_mode(ppu, VBLANK);
                        log_event(VBLANK_IRQ, "irq");
                        *interrupt_flag |= (1 << 0);
                        /* printf("Vblank interrupt\n"); */
                } else {
                        switch_to_mode(ppu, OAM_SCAN);
                }

        }
}

static void new_vblank(struct ppu *ppu, u8 *interrupt_flag)
{
        if (ppu->ly == 153 && ppu->dots_since_scanline_started == 4) {
                ppu->ly = 0;
                update_coincidence_flag(ppu);
                check_stat(ppu, interrupt_flag);
        } else if (ppu->dots_since_scanline_started == 455) {
                if (ppu->ly == 0) {
                        init_new_scanline(ppu);
                        switch_to_mode(ppu, OAM_SCAN);

                        assert(ppu->dots_since_frame_started == 70223);
                        ppu->frame++;
                        ppu->dots_since_frame_started = -1;
                } else {
                        ppu->ly++;
                        update_coincidence_flag(ppu);
                        check_stat(ppu, interrupt_flag);
                }
                ppu->dots_since_scanline_started = -1;
        }
}

static void new_oam_scan(struct ppu *ppu)
{
        /* TODO: different behaviour after LCD re-enabled */
        assert(ppu->dots_since_scanline_started < 80);
        assert(ppu->obj_buf_len == 0);

        /* for now, just scan for all objects at the end of mode 2 */
        if (ppu->dots_since_scanline_started != 79)
                return;

        u8 obj_height = (ppu->lcdc & OBJ_SIZE) ? 16 : 8;

        /* obj priority: lowest x first, and then oam index */
        for (u8 *p = ppu->oam; p < ppu->oam + 160; p += 4) {
                u8 obj_y = *p - 16;
                if (ppu->ly >= obj_y && ppu->ly < obj_y + obj_height) {
                        struct obj obj = {
                                .y          = p[0],
                                .x          = p[1],
                                .tile_index = p[2],
                                .attributes = p[3],
                        };

                        ppu->obj_buf[ppu->obj_buf_len] = obj;
                        log_event(TEMP, "oam scan y = %d, x = %d, ind = %x",
                                  obj.y, obj.x, obj.tile_index);

                        if (++ppu->obj_buf_len == 10)
                                break;
                }
        }
        for (int i = 1; i < ppu->obj_buf_len; i++)
                for (int j = i; j > 0; j--)
                        if (ppu->obj_buf[j - 1].x > ppu->obj_buf[j].x) {
                                struct obj temp     = ppu->obj_buf[j - 1];
                                ppu->obj_buf[j - 1] = ppu->obj_buf[j];
                                ppu->obj_buf[j]     = temp;
                        }

        /* set up for mode 3 */
        set_vram_access(ppu, false);
        switch_to_mode(ppu, DRAWING);
}

static u16 vram_offset_to_addr(u16 offset)
{
        return 0x8000 + offset;
}

static void vblank(struct ppu *ppu, u8 *interrupt_flag);
static void drawing(struct ppu *ppu, u8 *interrupt_flag);

static void oam_dma(struct ppu *ppu);

static struct fifo_entry pop_fifo(struct fifo *fifo)
{
        assert(fifo->len > 0);

        struct fifo_entry e = fifo->entries[fifo->head];

        fifo->head = (fifo->head + 1) & 7;
        fifo->len--;

        return e;
}

static void push_to_fifo(struct fifo *fifo, struct fifo_entry e)
{
        assert(fifo->len < 8);

        fifo->entries[(fifo->head + fifo->len) & 7] = e;
        fifo->len++;
}

static u8 get_bg_bitplane(struct ppu *ppu, u8 tile_id, u16 initial_offset)
{
        initial_offset |= !((ppu->lcdc & 0x10) || (tile_id & 0x80)) << 12;
        initial_offset |= tile_id << 4;
        initial_offset |= ((ppu->ly + ppu->scy) & 0x7) << 1;

        assert(vram_offset_to_addr(initial_offset) >= TILE_DATA_START &&
               vram_offset_to_addr(initial_offset) <= TILE_DATA_END);

        return ppu->vram[initial_offset];
}

static u8 get_obj_bitplane(struct ppu *ppu, u8 tile_id, u16 initial_offset)
{
        static const u8 reversed[256] = {
                0x00, 0x80, 0x40, 0xC0, 0x20, 0xA0, 0x60, 0xE0,
                0x10, 0x90, 0x50, 0xD0, 0x30, 0xB0, 0x70, 0xF0,
                0x08, 0x88, 0x48, 0xC8, 0x28, 0xA8, 0x68, 0xE8,
                0x18, 0x98, 0x58, 0xD8, 0x38, 0xB8, 0x78, 0xF8,
                0x04, 0x84, 0x44, 0xC4, 0x24, 0xA4, 0x64, 0xE4,
                0x14, 0x94, 0x54, 0xD4, 0x34, 0xB4, 0x74, 0xF4,
                0x0C, 0x8C, 0x4C, 0xCC, 0x2C, 0xAC, 0x6C, 0xEC,
                0x1C, 0x9C, 0x5C, 0xDC, 0x3C, 0xBC, 0x7C, 0xFC,
                0x02, 0x82, 0x42, 0xC2, 0x22, 0xA2, 0x62, 0xE2,
                0x12, 0x92, 0x52, 0xD2, 0x32, 0xB2, 0x72, 0xF2,
                0x0A, 0x8A, 0x4A, 0xCA, 0x2A, 0xAA, 0x6A, 0xEA,
                0x1A, 0x9A, 0x5A, 0xDA, 0x3A, 0xBA, 0x7A, 0xFA,
                0x06, 0x86, 0x46, 0xC6, 0x26, 0xA6, 0x66, 0xE6,
                0x16, 0x96, 0x56, 0xD6, 0x36, 0xB6, 0x76, 0xF6,
                0x0E, 0x8E, 0x4E, 0xCE, 0x2E, 0xAE, 0x6E, 0xEE,
                0x1E, 0x9E, 0x5E, 0xDE, 0x3E, 0xBE, 0x7E, 0xFE,
                0x01, 0x81, 0x41, 0xC1, 0x21, 0xA1, 0x61, 0xE1,
                0x11, 0x91, 0x51, 0xD1, 0x31, 0xB1, 0x71, 0xF1,
                0x09, 0x89, 0x49, 0xC9, 0x29, 0xA9, 0x69, 0xE9,
                0x19, 0x99, 0x59, 0xD9, 0x39, 0xB9, 0x79, 0xF9,
                0x05, 0x85, 0x45, 0xC5, 0x25, 0xA5, 0x65, 0xE5,
                0x15, 0x95, 0x55, 0xD5, 0x35, 0xB5, 0x75, 0xF5,
                0x0D, 0x8D, 0x4D, 0xCD, 0x2D, 0xAD, 0x6D, 0xED,
                0x1D, 0x9D, 0x5D, 0xDD, 0x3D, 0xBD, 0x7D, 0xFD,
                0x03, 0x83, 0x43, 0xC3, 0x23, 0xA3, 0x63, 0xE3,
                0x13, 0x93, 0x53, 0xD3, 0x33, 0xB3, 0x73, 0xF3,
                0x0B, 0x8B, 0x4B, 0xCB, 0x2B, 0xAB, 0x6B, 0xEB,
                0x1B, 0x9B, 0x5B, 0xDB, 0x3B, 0xBB, 0x7B, 0xFB,
                0x07, 0x87, 0x47, 0xC7, 0x27, 0xA7, 0x67, 0xE7,
                0x17, 0x97, 0x57, 0xD7, 0x37, 0xB7, 0x77, 0xF7,
                0x0F, 0x8F, 0x4F, 0xCF, 0x2F, 0xAF, 0x6F, 0xEF,
                0x1F, 0x9F, 0x5F, 0xDF, 0x3F, 0xBF, 0x7F, 0xFF,
        };

        struct obj obj = ppu->obj_buf[ppu->obj_index];

        u8 y_bits = ppu->ly - (obj.y - 16);

        /* vertical flip */
        if ((obj.attributes >> 6) & 0x1)
                y_bits = ~y_bits;

        initial_offset |= tile_id << 4;
        initial_offset |= (y_bits & 0x7) << 1;

        assert(vram_offset_to_addr(initial_offset) >= TILE_DATA_START &&
               vram_offset_to_addr(initial_offset) <= TILE_DATA_END);

        u8 bitplane = ppu->vram[initial_offset];

        /* horizontal flip */
        if ((obj.attributes >> 5) & 0x1)
                bitplane = reversed[bitplane];

        return bitplane;
}

static void load_bg_fifo(struct ppu *ppu)
{
        assert(ppu->bg_fifo.len == 0);
        struct fifo_entry e;
        for (int bit = 7; bit >= 0; bit--) {
                u8 low     = (ppu->bg_fetcher.bitplane0 >> bit) & 0x1;
                u8 high    = (ppu->bg_fetcher.bitplane1 >> bit) & 0x1;
                e.color_id = (u8)(low | high << 1);
                push_to_fifo(&ppu->bg_fifo, e);
        }
        assert(ppu->bg_fifo.len == 8);
}

static void load_obj_fifo(struct ppu *ppu)
{
        /* NOTE: obj fifo we should always reload it */
        assert(ppu->obj_fifo.len == 0);
        struct fifo_entry e;
        struct obj obj = ppu->obj_buf[ppu->obj_index];
        for (int bit = 7; bit >= 0; bit--) {
                u8 low     = (ppu->obj_fetcher.bitplane0 >> bit) & 0x1;
                u8 high    = (ppu->obj_fetcher.bitplane1 >> bit) & 0x1;
                e.color_id = (u8)(low | high << 1);
                e.palette  = (obj.attributes >> 4) & 0x1;
                e.priority = (obj.attributes >> 7) & 0x1;
                push_to_fifo(&ppu->obj_fifo, e);
        }
        assert(ppu->obj_fifo.len == 8);
}

static void fetch_bg_tile_id_idle(struct ppu *ppu)
{
        ppu->bg_fetcher.fn = fetch_bg_tile_id;
        log_event(FIFO, "fetch_bg_tile_id_idle");
}

static void fetch_bg_tile_id(struct ppu *ppu)
{
        u8 nametable = !!(ppu->lcdc & BG_NAMETABLE);
        u8 y         = (u8)(ppu->ly + ppu->scy) / 8;
        u8 x         = (u8)(ppu->pixel_count + ppu->scx) / 8;

        u16 offset = 0x1800 | nametable << 10 | y << 5 | x;

        assert(vram_offset_to_addr(offset) >= TILEMAP1_START &&
               vram_offset_to_addr(offset) <= TILEMAP2_END);

        ppu->bg_fetcher.tile_id = ppu->vram[offset];

        ppu->bg_fetcher.fn = fetch_bg_bitplane0_idle;
        log_event(FIFO, "fetch_bg_tile_id");
}

static void fetch_bg_bitplane0_idle(struct ppu *ppu)
{
        ppu->bg_fetcher.fn = fetch_bg_bitplane0;
        log_event(FIFO, "fetch_bg_bitplane0_idle");
}

static void fetch_bg_bitplane0(struct ppu *ppu)
{
        ppu->bg_fetcher.bitplane0 = get_bg_bitplane(ppu,
                                                    ppu->bg_fetcher.tile_id,
                                                    0);

        ppu->bg_fetcher.fn = fetch_bg_bitplane1_idle;
        log_event(FIFO, "fetch_bg_bitplane0");
}

static void fetch_bg_bitplane1_idle(struct ppu *ppu)
{
        ppu->bg_fetcher.fn = fetch_bg_bitplane1;
        log_event(FIFO, "fetch_bg_bitplane1_idle");
}

static void fetch_bg_bitplane1(struct ppu *ppu)
{
        ppu->bg_fetcher.bitplane1 = get_bg_bitplane(ppu,
                                                    ppu->bg_fetcher.tile_id,
                                                    1);

        ppu->bg_fetcher.fn = bg_push;
        log_event(FIFO, "fetch_bg_bitplane1");

        if (!ppu->initial_fetch_completed) {
                load_bg_fifo(ppu);
                ppu->initial_fetch_completed = true;
                ppu->bg_fetcher.fn = fetch_bg_tile_id_idle;
        } else if (ppu->obj_encountered) {
                /* NOTE: fetch_tile_id instead of fetch_tile_id_idle as the
                   first obj cycle overlaps the last background cycle. */
                log_event(FIFO, "obj fetch done");
                ppu->active_fetcher = OBJ_FETCHER;
                ppu->obj_fetcher.fn = fetch_obj_tile_id;
        }
}

static void bg_push(struct ppu *ppu)
{
        if (!ppu->obj_encountered) {
                if (ppu->bg_fifo.len == 0) {
                        load_bg_fifo(ppu);
                        ppu->bg_fetcher.fn = fetch_bg_tile_id_idle;
                        log_event(FIFO, "BG - PUSH (succeeded)");
                } else {
                        log_event(FIFO, "BG - PUSH (attempted)");
                }
        } else {
                if (ppu->bg_fifo.len == 0) {
                        log_event(FIFO, "weirddddddddd 111!");
                        load_bg_fifo(ppu);
                        ppu->bg_fetcher.fn = fetch_bg_tile_id_idle;
                        ppu->active_fetcher = OBJ_FETCHER;
                        ppu->obj_fetcher.fn = fetch_obj_tile_id_idle;
                } else {
                        log_event(FIFO, "weirddddddddd 222!");
                        ppu->bg_fetcher.fn = bg_push;
                        ppu->active_fetcher = OBJ_FETCHER;
                        ppu->obj_fetcher.fn = fetch_obj_tile_id_idle;
                }
        }

}

static void fetch_obj_tile_id_idle(struct ppu *ppu)
{
        ppu->obj_fetcher.fn = fetch_obj_tile_id;
        log_event(FIFO, "fetch_obj_tile_id_idle");
}

static void fetch_obj_tile_id(struct ppu *ppu)
{
        struct obj obj = ppu->obj_buf[ppu->obj_index];

        ppu->obj_fetcher.tile_id = obj.tile_index;

        if (ppu->lcdc & OBJ_SIZE) {
                if (ppu->ly - (obj.y - 16) < 8)
                        ppu->obj_fetcher.tile_id &= 0xFE;
                else
                        ppu->obj_fetcher.tile_id |= 0x01;
        }

        ppu->obj_fetcher.fn = fetch_obj_bitplane0_idle;
        log_event(FIFO, "fetch_obj_tile_id");
}

static void fetch_obj_bitplane0_idle(struct ppu *ppu)
{
        ppu->obj_fetcher.fn = fetch_obj_bitplane0;
        log_event(FIFO, "fetch_obj_bitplane0_idle");
}

static void fetch_obj_bitplane0(struct ppu *ppu)
{
        ppu->obj_fetcher.bitplane0 = get_obj_bitplane(ppu,
                                                      ppu->obj_fetcher.tile_id,
                                                      0);

        ppu->obj_fetcher.fn = fetch_obj_bitplane1_idle;
        log_event(FIFO, "fetch_obj_bitplane0");
}

static void fetch_obj_bitplane1_idle(struct ppu *ppu)
{
        ppu->obj_fetcher.fn = fetch_obj_bitplane1;
        log_event(FIFO, "fetch_obj_bitplane1_idle");
}

static void fetch_obj_bitplane1(struct ppu *ppu)
{
        ppu->obj_fetcher.bitplane1 = get_obj_bitplane(ppu,
                                                      ppu->obj_fetcher.tile_id,
                                                      1);

        ppu->obj_fetcher.fn = fetch_obj_bitplane1_idle;
        log_event(FIFO, "fetch_obj_bitplane0");

        /* do the obj push instantly after the fetch */

        ppu->ppobj_fifo.len = 0; /* TODO: remove this! */

        load_obj_fifo(ppu);
        ppu->obj_encountered  = false;
        ppu->obj_fetcher.fn = fetch_obj_tile_id_idle;
        ppu->active_fetcher      = BG_FETCHER;

        log_event(FIFO, "obj push");
}

static void new_drawing(struct ppu *ppu)
{
        int obj_index = -1;
        if (ppu->lcdc & BG_WIN_ENABLE)
                for (int i = 0; i < ppu->obj_buf_len; i++)
                        if (ppu->obj_buf[i].x == ppu->pixel_count) {
                                obj_index = i;
                                break;
                        }

        if (obj_index != -1 &&
            !ppu->obj_encountered &&
            !ppu->obj_buf[obj_index].seen) {
                ppu->obj_buf[obj_index].seen = true;
                ppu->obj_index = obj_index;
                do_logging = true;
                ppu->obj_encountered = true;
                log_event(TEMP, "found an obj, its index was %d", ppu->obj_index);

                if (ppu->active_fetcher == BG_FETCHER && ppu->bg_fetcher.fn == bg_push) {
                        ppu->active_fetcher = OBJ_FETCHER;
                        ppu->obj_fetcher.fn = fetch_obj_tile_id_idle; /* TODO: check this! */
                }
        }

        if (ppu->initial_fetch_completed && !ppu->obj_encountered) {
                assert(ppu->dots_since_scanline_started >= 86);

                struct fifo_entry bg = pop_fifo(&ppu->bg_fifo);
                struct fifo_entry obj = {0};
                if (ppu->obj_fifo.len > 0)
                        obj = pop_fifo(&ppu->obj_fifo);

                log_event(FIFO, "FIFOs clocked: BG len = %d; OBJ len = %d",
                          ppu->bg_fifo.len, ppu->obj_fifo.len);

                if (!ppu->scx_pixels_dropped)
                        if (ppu->shift_count++ >= (ppu->scx & 0x7))
                                ppu->scx_pixels_dropped = true;

                if (ppu->scx_pixels_dropped && ppu->pixel_count++ >= 8) {
                        enum {OBJ_PX, BG_PX} selected_pixel = OBJ_PX;
                        if (ppu->lcdc & BG_WIN_ENABLE) {
                                if (!(ppu->lcdc & OBJ_ENABLE))
                                        selected_pixel = BG_PX;
                                else if (obj.priority && bg.color_id != 0)
                                        selected_pixel = BG_PX;
                                else if (obj.color_id == 0)
                                        selected_pixel = BG_PX;
                        }

                        u8 color   = bg.color_id;
                        u8 palette = ppu->bgp;
                        if (selected_pixel == OBJ_PX) {
                                color   = obj.color_id;
                                palette = obj.palette ? ppu->obp1 : ppu->obp0;
                        }

                        color = (palette >> (color * 2)) & 0x3;
                        if (!(ppu->lcdc & BG_WIN_ENABLE))
                                color = 0;

                        u32 gui_color = ppu->palette[color];
                        int pos = ppu->ly * 160 + ppu->pixel_count - 9;
                        ppu->display_buf[pos] = gui_color;
                        if (ppu->pixel_count == 168) {
                                set_vram_access(ppu, true);
                                /* set_oam_access(ppu, true); */
                                switch_to_mode(ppu, HBLANK);
                                return;
                        }
                }
        }

        if (ppu->active_fetcher == BG_FETCHER)
                ppu->bg_fetcher.fn(ppu);
        else
                ppu->obj_fetcher.fn(ppu);
}

/* NOTE: for now, don't implement VRAM blocking */
static void write_vram(struct ppu *ppu, u8 v, u16 addr)
{
        /* ppu->vram[addr - VRAM_START] = v; /\* NOTE! *\/ */
        if (ppu->vram_accessible)
                ppu->vram[addr - VRAM_START] = v;
}

static u8 read_vram(struct ppu *ppu, u16 addr)
{
        /* return ppu->vram[addr - VRAM_START]; /\* NOTE! *\/ */
        return ppu->vram_accessible ? ppu->vram[addr - VRAM_START] : 0xFF;
}

static void write_oam(struct ppu *ppu, u8 v, u16 addr)
{
        if (ppu->oam_accessible)
                ppu->oam[addr - OAM_START] = v;
}

static u8 read_oam(struct ppu *ppu, u16 addr)
{
        return ppu->oam_accessible ? ppu->oam[addr - OAM_START] : 0xFF;
}

static void oam_dma(struct ppu *ppu)
{
        if (!(ppu->dma_in_progress || ppu->dma_requested))
                return;

        if (ppu->dma_requested) {
                if (ppu->cycles_since_dma_requested > 1) {
                        ppu->dma_offset      = 0;
                        ppu->dma_requested   = false;
                        ppu->dma_in_progress = true;
                } else {
                        ppu->cycles_since_dma_requested++;
                        if (ppu->cycles_since_dma_requested == 2)
                                set_oam_access(ppu, false);
                        if (!ppu->dma_in_progress)
                                return;
                }
        }

        assert(ppu->dma_offset >= 0 && ppu->dma_offset < 160);

        u16 addr = (ppu->dma << 8) + ppu->dma_offset;
        ppu->oam[ppu->dma_offset++] = read_mem(ppu->mem, addr);

        if (ppu->dma_offset == 160) {
                ppu->dma_in_progress = false;
                set_oam_access(ppu, true);
        }
}

static bool check_stat(struct ppu *ppu, u8 *interrupt_flag)
{
        bool stat_line = false;
        int bit=0;

        if (read_ppu_reg(ppu, LY_ADDR) == ppu->lyc && (ppu->stat & (1 << 6))) {
                bit = 6;
                stat_line = true;
        } else if (ppu->mode == 2 && (ppu->stat & (1 << 5))) {
                bit = 5;
                stat_line = true;
        } else if (ppu->mode == 1 && (ppu->stat & (1 << 4))) {
                bit = 4;
                stat_line = true;
        } else if (ppu->mode == 0 && (ppu->stat & (1 << 3))) {
                bit = 3;
                stat_line = true;
        }

        bool interrupt_pending = stat_line && !ppu->prev_stat_line;
        ppu->prev_stat_line    = stat_line;

        if (interrupt_pending) {
                *interrupt_flag |= (1 << 1);
                if (bit == 6)
                        log_event(STAT_IRQ, "lyc irq (lyc = %d)", ppu->lyc);
                else
                        log_event(STAT_IRQ, "mode %c irq", '0' + bit - 3);
        }

        return interrupt_pending;
}

static void write_ppu_reg(struct ppu *ppu, u8 v, u16 addr)
{
        switch(addr) {
        case LCDC_ADDR:;
                bool lcd_was_enabled = ppu->lcdc & LCD_ENABLE;
                bool lcd_enabled = v & LCD_ENABLE;
                if (lcd_was_enabled && !lcd_enabled)
                        log_event(LCD_TOGGLE, "LCD turned off");

                if (!lcd_was_enabled && lcd_enabled)
                        log_event(LCD_TOGGLE, "LCD turned on");

                log_event(LCDC_WRITE,
                          "set to 0x%X = "BIN_FMT" (was 0x%X = "BIN_FMT")",
                          v, BIN(v), ppu->lcdc, BIN(ppu->lcdc));

                if (!lcd_was_enabled && lcd_enabled) {
                        ppu->mode = OAM_SCAN;
                }
                /* if (!(ppu->lcdc >> 7) && (v >> 7)) { */
                /*         ppu->lcd_reenabled = true; */
                /*         ppu->mode = OAM_SCAN; */
                /* } */
                ppu->lcdc = v;
                break;
        case STAT_ADDR:;
                u8 prev = ppu->stat;
                log_event(STAT_WRITE, "raw value is %d", v);
                ppu->stat = (v & ~0x7) | (ppu->stat & 0x7);
                ppu->stat |= (1 << 7);
                log_event(STAT_WRITE,
                          "set to 0x%X = "BIN_FMT" (was 0x%X = "BIN_FMT")",
                          ppu->stat, BIN(ppu->stat), prev, BIN(prev));
                break;
        case SCY_ADDR:
                ppu->scy = v;
                break;
        case SCX_ADDR:
                log_event(SCX_WRITE, "set to %d = %x (was %d = %x)",
                          v, v, ppu->scx, ppu->scx);
                ppu->scx = v;
                break;
        case LY_ADDR:
                ppu->ly = v;
                break;
        case LYC_ADDR:
                log_event(LYC_WRITE, "set to %d (was %d)", v, ppu->lyc);
                ppu->lyc = v;
                update_coincidence_flag(ppu);
                break;
        case DMA_ADDR:
                assert(v <= 0xDF); /* TODO: check what happens here? */
                ppu->dma = v;
                ppu->dma_requested= true;
                ppu->cycles_since_dma_requested = 0;
                break;
        case BGP_ADDR:
                log_event(BGP_WRITE, "set to %d (was %d)", v, ppu->bgp);
                ppu->bgp = v;
                break;
        case OBP0_ADDR:
                ppu->obp0 = v;
                break;
        case OBP1_ADDR:
                ppu->obp1 = v;
                break;
        case WX_ADDR:
                if (v == 0 || v == 166)
                        die("write_wx: set to unreliable value %d\n", v);
                ppu->wx = v;
                break;
        case WY_ADDR:
                ppu->wy = v;
                break;
        }
}

static u8 read_ppu_reg(struct ppu *ppu, u16 addr)
{
        switch(addr) {
        case LCDC_ADDR:
                return ppu->lcdc;
        case STAT_ADDR:
                return ppu->stat;
        case SCY_ADDR:
                return ppu->scy;
        case SCX_ADDR:
                return ppu->scx;
        case LY_ADDR:
                return ppu->ly;
        case LYC_ADDR:
                return ppu->lyc;
        case DMA_ADDR:
                return ppu->dma;
        case BGP_ADDR:
                return ppu->bgp;
        case OBP0_ADDR:
                return ppu->obp0;
        case OBP1_ADDR:
                return ppu->obp1;
        case WX_ADDR:
                return ppu->wx;
        case WY_ADDR:
                return ppu->wy;
        }

        assert(false);
        return 0;
}
