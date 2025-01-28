#define BIN_FMT "%c%c%c%c'%c%c%c%c"
#define BIN(x)                                                  \
        ('0' + ((x >> 7) & 0x1)), ('0' + ((x >> 6) & 0x1)),     \
        ('0' + ((x >> 5) & 0x1)), ('0' + ((x >> 4) & 0x1)),     \
        ('0' + ((x >> 3) & 0x1)), ('0' + ((x >> 2) & 0x1)),     \
        ('0' + ((x >> 1) & 0x1)), ('0' + ((x >> 0) & 0x1))

static bool do_logging;

#define log_event(event, ...)                                           \
        do {                                                            \
        if (PPU_LOGGING_ENABLED && LOG_PPU_##event) {                   \
                if (do_logging) {                                       \
                        printf("frame = %3d, "                          \
                               "mode = %d, "                            \
                               "ly = %3d, "                             \
                               "lx = %3d, "                             \
                               "dot = %3d "                             \
                               "dots-since-frame = %3d "                \
                               "stat = "BIN_FMT" "                      \
                               "[%s]    \t",                            \
                               ppu->frame,                              \
                               ppu->mode,                               \
                               ppu->ly,                                 \
                               ppu->lx,                                 \
                               ppu->dots_since_scanline_started,        \
                               ppu->dots_since_frame_started,           \
                               BIN(ppu->stat),                          \
                               #event);                                 \
                        printf(__VA_ARGS__);                            \
                        putchar('\n');                                  \
                }                                                       \
        }                                                               \
        } while (0)

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
        if (!(ppu->lcdc >> 7)) {
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
        ppu->sprite_encountered = false;

        ppu->initial_fetch_completed = false;
        ppu->scx_pixels_dropped = false;

        memset(&ppu->obj_fifo, 0, sizeof ppu->obj_fifo);
        memset(&ppu->bg_fifo, 0, sizeof ppu->bg_fifo);
        memset(&ppu->obj_fetcher, 0, sizeof ppu->obj_fetcher);
        memset(&ppu->bg_fetcher, 0, sizeof ppu->bg_fetcher);

        ppu->shift_count = 0;
        ppu->pixel_count = 0;
        ppu->lx = 0;

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
        }

        if (ppu->dots_since_scanline_started == 455) {
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
        /* TODO: Different behaviour after LCD re-enabled */
        assert(ppu->dots_since_scanline_started < 80);

        if (ppu->dots_since_scanline_started == 79) {

                /* just do it all in one go at the end */
                ppu->nslots = 0;

                u8 sprite_height = (ppu->lcdc & (1 << 2)) ? 16 : 8;
                if (sprite_height == 16)
                        ; /* TODO */

                for (u8 *p = ppu->oam; p < ppu->oam + 160; p += 4) {
                        u8 y = *p - 16;
                        if (ppu->ly >= y && ppu->ly < y + sprite_height) {
                                ppu->obj_slots[ppu->nslots++] = (struct obj) {
                                        .y          = p[0],
                                        .x          = p[1],
                                        .tile_index = p[2],
                                        .attributes = p[3],
                                };

                                log_event(TEMP, "oam scan y = %d, x = %d, ind = %x",
                                        ppu->obj_slots[ppu->nslots-1].y,
                                        ppu->obj_slots[ppu->nslots-1].x,
                                        ppu->obj_slots[ppu->nslots-1].tile_index);

                                if (ppu->nslots == 10)
                                        break;
                        }
                }

                /* set up for mode 3 */
                set_vram_access(ppu, false);
                switch_to_mode(ppu, DRAWING);
        }
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

static void fetch_bg_tile_id(struct ppu *ppu)
{
        u8 nametable = ((ppu->lcdc >> 3) & 0x1);
        u8 y         = (u8)(ppu->ly + ppu->scy) / 8;
        u8 x         = (u8)(ppu->pixel_count + ppu->scx) / 8;

        u16 offset = 0x1800 | nametable << 10 | y << 5 | x;

        assert(vram_offset_to_addr(offset) >= TILEMAP1_START &&
               vram_offset_to_addr(offset) <= TILEMAP2_END);

        ppu->bg_fetcher.tile_id = ppu->vram[offset];
}

static void fetch_obj_tile_id(struct ppu *ppu)
{
        ppu->obj_fetcher.tile_id = ppu->obj_slots[ppu->obj_index].tile_index;
}

static u16 bg_bitplane_formula(struct ppu *ppu, u8 tile_id)
{
        u16 offset = 0;
        offset |= !((ppu->lcdc & 0x10) || (tile_id & 0x80)) << 12;
        offset |= tile_id << 4;
        offset |= ((ppu->ly + ppu->scy) & 0x7) << 1;
        return offset;
}

static u16 obj_bitplane_formula(struct ppu *ppu, u8 tile_id)
{
        u16 offset = 0;
        offset |= tile_id << 4;
        offset |= ((ppu->ly + ppu->scy) & 0x7) << 1;
        return offset;
}

static void fetch_bg_bitplane0(struct ppu *ppu)
{
        u8 tile_id = ppu->bg_fetcher.tile_id;
        u16 offset = bg_bitplane_formula(ppu, tile_id);

        assert(vram_offset_to_addr(offset) >= TILE_DATA_START &&
               vram_offset_to_addr(offset) <= TILE_DATA_END);

        ppu->bg_fetcher.bitplane0 = ppu->vram[offset];
}

static void fetch_obj_bitplane0(struct ppu *ppu)
{
        u8 tile_id = ppu->obj_fetcher.tile_id;
        u16 offset = (ppu->lcdc & (1 << 4)) ?
                (tile_id << 4) | (((ppu->ly + ppu->scy) & 0x7) << 1) :
                (0x1000 - (tile_id << 4)) | (((ppu->ly + ppu->scy) & 0x7) << 1);

        assert(vram_offset_to_addr(offset) >= TILE_DATA_START &&
               vram_offset_to_addr(offset) <= TILE_DATA_END);

        ppu->obj_fetcher.bitplane0 = ppu->vram[offset];
}

static void fetch_bg_bitplane1(struct ppu *ppu)
{
        u8 tile_id = ppu->bg_fetcher.tile_id;
        u16 offset = 1 + bg_bitplane_formula(ppu, tile_id);

        assert(vram_offset_to_addr(offset) >= TILE_DATA_START &&
               vram_offset_to_addr(offset) <= TILE_DATA_END);

        ppu->bg_fetcher.bitplane1 = ppu->vram[offset];
}

static void fetch_obj_bitplane1(struct ppu *ppu)
{
        u8 tile_id = ppu->obj_fetcher.tile_id;
        u16 offset = 1;
        offset |= (ppu->lcdc & (1 << 4)) ?
                (tile_id << 4) | (((ppu->ly + ppu->scy) & 0x7) << 1) :
                (0x1000 - (tile_id << 4)) | (((ppu->ly + ppu->scy) & 0x7) << 1);

        assert(vram_offset_to_addr(offset) >= TILE_DATA_START &&
               vram_offset_to_addr(offset) <= TILE_DATA_END);

        ppu->obj_fetcher.bitplane1 = ppu->vram[offset];
}

static void load_obj_fifo(struct ppu *ppu)
{
        /* NOTE: obj fifo we should always reload it */
        assert(ppu->obj_fifo.len == 0);
        struct fifo_entry e;
        for (int bit = 7; bit >= 0; bit--) {
                u8 low    = ((ppu->obj_fetcher.bitplane0 >> bit) & 1);
                u8 high   = ((ppu->obj_fetcher.bitplane1 >> bit) & 1);
                e.color   = (u8)(low | high << 1);
                push_to_fifo(&ppu->obj_fifo, e);
        }
        assert(ppu->obj_fifo.len == 8);
}

static void load_bg_fifo(struct ppu *ppu)
{
        assert(ppu->bg_fifo.len == 0);
        struct fifo_entry e;
        for (int bit = 7; bit >= 0; bit--) {
                u8 low    = ((ppu->bg_fetcher.bitplane0 >> bit) & 1);
                u8 high   = ((ppu->bg_fetcher.bitplane1 >> bit) & 1);
                e.color   = (u8)(low | high << 1);
                push_to_fifo(&ppu->bg_fifo, e);
        }
        assert(ppu->bg_fifo.len == 8);
}

static void tick_drawing(struct ppu *ppu, u8 *interrupt_flag);

static int check_obj(struct ppu *ppu)
{
        for (int i = 0; i < ppu->nslots; i++) {
                int x = ppu->obj_slots[i].x;
                /* TODO: use ppu->pixel_count instead? */
                if (x >= ppu->lx && x <= ppu->lx + 8) {
                        if (ppu->obj_slots[i].seen) {
                                return -1;
                        } else {
                                ppu->obj_slots[i].seen = true;
                                return i;
                        }
                }
        }
        return -1;
}

static void new_drawing(struct ppu *ppu)
{
        /* assert(ppu->active_fetcher == BG_FETCHER); */

        /* printf("dots are %d lx is %d\n", ppu->dots_since_scanline_started, ppu->lx); */

        if (!ppu->sprite_encountered && (ppu->obj_index = check_obj(ppu)) != -1) {
                do_logging = true;
                ppu->sprite_encountered = true;
                log_event(TEMP, "found a sprite, its index was %d", ppu->obj_index);
        }


        if (ppu->initial_fetch_completed && !ppu->sprite_encountered) {
                assert(ppu->dots_since_scanline_started >= 86);

                struct fifo_entry bg = pop_fifo(&ppu->bg_fifo);

                log_event(TEMP, "pop!");

                struct fifo_entry obj;

                bool use_obj = false;

                if (ppu->obj_fifo.len > 0) {
                        obj = pop_fifo(&ppu->obj_fifo);
                        log_event(TEMP, "pop obj!");
                        use_obj = true;
                }

                if (!ppu->scx_pixels_dropped)
                        if (ppu->shift_count++ >= (ppu->scx & 0x7))
                                ppu->scx_pixels_dropped = true;

                if (ppu->scx_pixels_dropped && ppu->pixel_count++ >= 8) {
                        u8 color;
                        color = (ppu->bgp >> (bg.color * 2)) & 0x3;

                        if (use_obj)
                                color = obj.color;

                        if ((ppu->lcdc & 0x1) == 0)
                                color = 0;
                        u32 gui_color = ppu->palette[color];
                        int pos = ppu->ly * 160 + ppu->lx;
                        /* log_event(TEMP, "pushed pxiel to the lcd, lx++"); */
                        ppu->lx++;
                        ppu->display_buf[pos] = gui_color;
                        if (ppu->pixel_count == 168) {
                                /* log_event(MODE3_TIMING, "Took %d dots", ppu->dots_since_scanline_started); */
                                set_vram_access(ppu, true);
                                /* set_oam_access(ppu, true); */
                                switch_to_mode(ppu, HBLANK);
                                return;
                        }
                }
        }

        if (!ppu->sprite_encountered && (ppu->obj_index = check_obj(ppu)) != -1) {
                do_logging = true;
                ppu->sprite_encountered = true;
                log_event(TEMP, "found a sprite, its index was %d", ppu->obj_index);
        }

        if (ppu->active_fetcher == BG_FETCHER) {
                switch(ppu->bg_fetcher.state) {
                case FETCH_TILE_ID_IDLE: // 80, 86
                        ppu->bg_fetcher.state = FETCH_TILE_ID;
                        break;
                case FETCH_TILE_ID: // 81, 87
                        fetch_bg_tile_id(ppu);
                        ppu->bg_fetcher.state = FETCH_BITPLANE0_IDLE;
                        break;
                case FETCH_BITPLANE0_IDLE: // 82, 88
                        ppu->bg_fetcher.state = FETCH_BITPLANE0;
                        break;
                case FETCH_BITPLANE0: // 83, 89
                        fetch_bg_bitplane0(ppu);
                        ppu->bg_fetcher.state = FETCH_BITPLANE1_IDLE;
                        break;
                case FETCH_BITPLANE1_IDLE: // 84, 80
                        ppu->bg_fetcher.state = FETCH_BITPLANE1;
                        break;
                case FETCH_BITPLANE1: // 85, 91
                        fetch_bg_bitplane1(ppu);
                        if (!ppu->initial_fetch_completed) {
                                load_bg_fifo(ppu);

                                ppu->initial_fetch_completed = true;
                                ppu->bg_fetcher.state = FETCH_TILE_ID_IDLE;
                        } else {
                                if (ppu->sprite_encountered) {
                                        log_event(TEMP, "fcnt==5, switched to obj fetcher");
                                        /* FCNT == 5, now we do the sprite fetch */
                                        ppu->active_fetcher = OBJ_FETCHER;
                                        ppu->obj_fetcher.state = FETCH_TILE_ID_IDLE;
                                        /* log_event(TEMP, "active fetcher now obj fetcher"); */

                                        /* when we finish the sprite fetch out
                                           of this we do a push */
                                        ppu->bg_fetcher.state = PUSH;
                                } else {
                                        ppu->bg_fetcher.state = PUSH;
                                }
                        }
                        break;
                case PUSH:
                        assert(!ppu->sprite_encountered);
                        if (!ppu->sprite_encountered) {
                                if (ppu->bg_fifo.len == 0) {
                                        load_bg_fifo(ppu);
                                        ppu->bg_fetcher.state = FETCH_TILE_ID_IDLE;
                                } else {
                                        /* will try again next time */
                                        /* log_event(TEMP, "will try again next time"); */
                                }
                        } else {
                                if (ppu->bg_fifo.len == 0) {
                                        log_event(TEMP, "weirddddddddd 111!");
                                        load_bg_fifo(ppu);
                                        ppu->bg_fetcher.state = FETCH_TILE_ID_IDLE;
                                        ppu->active_fetcher = OBJ_FETCHER;
                                        ppu->obj_fetcher.state = FETCH_TILE_ID_IDLE;
                                } else {
                                        log_event(TEMP, "weirddddddddd 222!");
                                        ppu->bg_fetcher.state = PUSH;
                                        ppu->active_fetcher = OBJ_FETCHER;
                                        ppu->obj_fetcher.state = FETCH_TILE_ID_IDLE;
                                }
                        }
                        break;
                }
        } else {
                switch(ppu->obj_fetcher.state) {
                case FETCH_TILE_ID_IDLE: // 80, 86
                        ppu->obj_fetcher.state = FETCH_TILE_ID;
                        break;
                case FETCH_TILE_ID: // 81, 87
                        fetch_obj_tile_id(ppu);
                        ppu->obj_fetcher.state = FETCH_BITPLANE0_IDLE;
                        log_event(TEMP, "obj fetch tile id ");
                        break;
                case FETCH_BITPLANE0_IDLE: // 82, 88
                        ppu->obj_fetcher.state = FETCH_BITPLANE0;
                        break;
                case FETCH_BITPLANE0: // 83, 89
                        fetch_obj_bitplane0(ppu);
                        ppu->obj_fetcher.state = FETCH_BITPLANE1_IDLE;
                        log_event(TEMP, "obj bitplane 0");
                        break;
                case FETCH_BITPLANE1_IDLE: // 84, 80
                        /* log_event(TEMP, TEMP, "one!"); */
                        ppu->obj_fetcher.state = FETCH_BITPLANE1;
                        break;
                case FETCH_BITPLANE1: // 85, 91
                        ppu->obj_fifo.len = 0;
                        fetch_obj_bitplane1(ppu);
                        ppu->obj_fetcher.state = PUSH;
                        log_event(TEMP, "obj bitplane 1");
                        break;
                case PUSH:
                        load_obj_fifo(ppu);
                        ppu->sprite_encountered = false;
                        log_event(TEMP, "sprite encountered = false");
                        ppu->obj_fetcher.state = FETCH_TILE_ID_IDLE; /* not needed ? */
                        ppu->active_fetcher = BG_FETCHER;
                        log_event(TEMP, "load obj fifo (push step)");
                        /* log_event(TEMP, "obj bitplane1 fetched + pushed"); */
                        /* I guess this is instant? */
                        break;
                        /*         /\* log_event(TEMP, "came ehre!"); *\/ */
                        /*         ppu->obj_fetcher.state = PUSH; */
                        /*         /\* exit(0); *\/ */
                        /*         break; */
                        /* case PUSH: */
                        /*         /\* just for now, assume no overlapping obj fetches *\/ */
                        /*         load_obj_fifo(ppu); */
                        /*         /\* ppu->obj_fetcher.state = FETCH_TILE_ID_IDLE; /\\* not really needed *\\/ *\/ */
                        /*         /\* ppu->active_fetcher. *\/ */
                        /* break */

                }
        }
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
                bool lcd_was_enabled = ppu->lcdc >> 7;
                bool lcd_enabled = v >> 7;
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
                /*         /\* die("bro"); *\/ */
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
                /* printf("Yep, DMA was written to on cycle %ld!\n", *TICK); */
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
