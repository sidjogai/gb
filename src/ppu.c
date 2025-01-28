#define dbg(...) do {                                                   \
                printf("tick = %lld ", ppu->line_delta);                \
                printf("[%s, oam blocking %s] ", __func__, ppu->oam_access_blocked ? "ON" : "OFF"); \
                printf(__VA_ARGS__);                                    \
        } while(0)

#undef dbg
#define dbg(...) ;

#define pf(...) do {printf("tick = %ld; dot = %d;", *TICK, ppu->dots_since_scanline_started);            \
                    printf(__VA_ARGS__); putchar('\n');}  while (0)

static bool ppu_loggin_enabled = true;
/* static bool ppu_loggin_enabled = false; */

/* #define GB_LOG_PPU */
#ifdef GB_LOG_PPU
#define log_ppu(...)                                    \
        do {                                            \
                if (!ppu_loggin_enabled)                \
                        break;                          \
                printf(                                 \
                       "tick = %-3d; "                \
                       "SCNT = %-3d; "                  \
                       "PCNT = %-3d; "                  \
                       "ly = %-3d; "                    \
                       "lx = %-3d; "                    \
                       "        %s:%-3d:%-22s        ", \
                       ppu->line_delta,                 \
                       ppu->shift_count,                \
                       ppu->pixel_count,                \
                       ppu->ly,                         \
                       ppu->lx,                         \
                       &__FILE__[2],                    \
                       __LINE__,                        \
                       __func__                         \
                       );                               \
                fprintf(stdout, __VA_ARGS__);            \
                putchar('\n');                          \
        } while(0)
#else
#define log_ppu(...) ;
#endif


static void switch_to_mode(struct ppu *ppu, enum ppu_mode m)
{
        pf("switched to mode %d now", m);
        ppu->mode = m;
        ppu->stat &= ~0x7;
        ppu->stat |= ppu->mode;
}

static void update_mode(struct ppu *ppu)
{
        ppu->stat &= ~0x7;
        ppu->stat |= ppu->mode;
}

static void step_dot(struct ppu *ppu, u8 *interrupt_flag);


static void new_oam_scan(struct ppu *ppu);
static void new_drawing(struct ppu *ppu);
static void new_hblank(struct ppu *ppu);
static void new_vblank(struct ppu *ppu);

static bool stat_interrupt_pending(struct ppu *ppu, u8 *interrupt_flag);

static void tick_ppu(struct ppu *ppu, u8 *interrupt_flag)
{
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
                new_hblank(ppu);
                break;
        case VBLANK:
                new_vblank(ppu);
                break;
        }

        if (stat_interrupt_pending(ppu, interrupt_flag)) {
                *interrupt_flag |= 1 << 1;
        }

        ppu->dots_since_scanline_started++;
}


/* ============================ mode 0 - HBLANK ============================= */

static void start_new_scanline(struct ppu *ppu)
{
        ppu->mode       = OAM_SCAN;
        ppu->nslots     = 0;
        ppu->lx         = 0;
        ppu->line_delta = 0;
}


static void new_vblank(struct ppu *ppu)
{
        if (ppu->dots_since_scanline_started == 455) {
                ppu->ly++;
                if (ppu->ly == 154) {
                        ppu->ly = 0;
                        /* Q; */
                        switch_to_mode(ppu, OAM_SCAN);
                } else {
                        /* nothing */
                }
                ppu->dots_since_scanline_started = -1;
        }
}


static void new_hblank(struct ppu *ppu)
{
        /* TODO: weird ly stuff */
        if (ppu->dots_since_scanline_started == 455) {
                pf("here");
                ppu->ly++;
                if (ppu->ly == 144) {
                        switch_to_mode(ppu, VBLANK);
                } else {
                        switch_to_mode(ppu, OAM_SCAN);
                }
                ppu->dots_since_scanline_started = -1;
        }
}

static void hblank(struct ppu *ppu, u8 *interrupt_flag)
{
        for (int i = 0; i < 4; i++) {
                if (ppu->line_delta == 456) {
                        if (++ppu->ly == 144) {
                                ppu->mode       = VBLANK;
                                ppu->line_delta = 0;
                                *interrupt_flag |= 1 << 0;
                                /* printf("triggered vblank interrupt at frame %d tick %d line_delta %d\n", *FRAME, *TICK, ppu->line_delta); */
                        } else {
                                /* printf("new scanline at frame %lld tick %lld line_delta %d\n", *FRAME, *TICK, ppu->line_delta); */
                                start_new_scanline(ppu);
                        }
                        update_mode(ppu);
                        return;
                }
                step_dot(ppu, interrupt_flag);
        }
}

/* ============================ mode 1 - VBLANK ============================= */

static void vblank(struct ppu *ppu, u8 *interrupt_flag)
{
        if (ppu->line_delta + 4 != 456)
                goto done;

        ppu->line_delta = 0;
        if (++ppu->ly == 154) {
                ppu->ly = 0;
                start_new_scanline(ppu);
        }

 done:
        step_dot(ppu, interrupt_flag);
        step_dot(ppu, interrupt_flag);
        step_dot(ppu, interrupt_flag);
        step_dot(ppu, interrupt_flag);
}

/* =========================== mode 2 - OAM scan ============================ */

static void new_oam_scan(struct ppu *ppu)
{
        /* TODO: Different behaviour after LCD re-enabled */

        assert(ppu->dots_since_scanline_started < 80);

        /* new OAM entry fetched every 2 dots */
        if (ppu->dots_since_scanline_started % 2 == 0)
                return;

        if (ppu->dots_since_scanline_started == 79) {
                ppu->pixel_counter_enabled = false;
                ppu->shift_counter_enabled = false;
                ppu->shift_count = 0;
                ppu->pixel_count = 0;

                
                ppu->active_fetcher = BG_FETCHER;
                ppu->in_first_fetch = true;
                ppu->initial_pixels_dropped = false;
                ppu->initial_delay = 0;
                memset(&ppu->obj_fifo, 0, sizeof ppu->obj_fifo);
                memset(&ppu->bg_fifo, 0, sizeof ppu->bg_fifo);
                memset(&ppu->obj_fetcher, 0, sizeof ppu->obj_fetcher);
                memset(&ppu->bg_fetcher, 0, sizeof ppu->bg_fetcher);

                switch_to_mode(ppu, DRAWING);
                pf("switched to drawing now");
        }
}

static void oam_scan(struct ppu *ppu, u8 *interrupt_flag)
{
        if (ppu->lcd_reenabled) {
                if (ppu->line_delta + 4 != 76) {
                        ppu->lcd_reenabled = false;
                        goto drawing;
                }
                goto done;
        }

        if (ppu->line_delta + 4 != 80)
                goto done;

        /* TODO: the sprite with the lowest x position "wins" */
        ppu->nslots = 0;

        u8 sprite_height = (ppu->lcdc & (1 << 2)) ? 16 : 8;
        if (sprite_height == 16){}
                /* TODO */

        for (u8 *p = ppu->oam; p < ppu->oam + 160; p += 4) {
                u8 y = *p - 16;
                if (ppu->ly >= y && ppu->ly < y + sprite_height) {
                        ppu->obj_slots[ppu->nslots++] = (struct obj) {
                                .y          = p[0],
                                .x          = p[1],
                                .tile_index = p[2],
                                .attributes = p[3],
                        };

                        if (ppu->nslots == 10)
                                break;
                }
        }

        log_ppu("nslots = %d (sprite height = %d)\n",
                ppu->nslots, sprite_height);

 drawing:
        ppu->pixel_counter_enabled = false;
        ppu->shift_counter_enabled = false;
        ppu->shift_count = 0;
        ppu->pixel_count = 0;

        ppu->mode = DRAWING;
        ppu->active_fetcher = BG_FETCHER;
        memset(&ppu->obj_fifo, 0, sizeof ppu->obj_fifo);
        memset(&ppu->bg_fifo, 0, sizeof ppu->bg_fifo);
        memset(&ppu->obj_fetcher, 0, sizeof ppu->obj_fetcher);
        memset(&ppu->bg_fetcher, 0, sizeof ppu->bg_fetcher);
        /* printf("ppu->lx %d\n", ppu->lx); */

        update_mode(ppu);
 done:
        step_dot(ppu, interrupt_flag);
        step_dot(ppu, interrupt_flag);
        step_dot(ppu, interrupt_flag);
        step_dot(ppu, interrupt_flag);
}

/* ============================ mode 3 - drawing ============================ */

static void step_dot(struct ppu *ppu, u8 *interrupt_flag);

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
        u8 n = (ppu->lcdc >> 3) & 1;
        u8 x = (ppu->scx + ppu->pixel_count) / 8;
        u8 y = (ppu->ly + ppu->scy) & 0xF8;

        u16 offset = 0x1800 | n << 10 | y << 2 | x;

        assert(x <= 0x3F);
        assert(vram_offset_to_addr(offset) >= TILEMAP1_START &&
               vram_offset_to_addr(offset) <= TILEMAP2_END);

        ppu->bg_fetcher.tile_id = ppu->vram[offset];
        /* log_ppu("Fetch bg tile ID"); */
}

static u16 bitplane_formula(struct ppu *ppu, u8 tile_id)
{
        u16 offset = 0;
        offset |= !((ppu->lcdc & 0x10) || (tile_id & 0x80)) << 12;
        offset |= tile_id << 4;
        offset |= ((ppu->ly + ppu->scy) & 0x7) << 1;
        return offset;
}

static void fetch_bg_bitplane0(struct ppu *ppu)
{
        u8 tile_id = ppu->bg_fetcher.tile_id;
        u16 offset = bitplane_formula(ppu, tile_id);

        assert(vram_offset_to_addr(offset) >= TILE_DATA_START &&
               vram_offset_to_addr(offset) <= TILE_DATA_END);
        /* log_ppu("offset = %x; bitplane = %d\n", offset, ppu->vram[offset]); */

        ppu->bg_fetcher.bitplane0 = ppu->vram[offset];
}

static void fetch_bg_bitplane1(struct ppu *ppu)
{
        u8 tile_id = ppu->bg_fetcher.tile_id;
        u16 offset = 1 + bitplane_formula(ppu, tile_id);

        assert(vram_offset_to_addr(offset) >= TILE_DATA_START &&
               vram_offset_to_addr(offset) <= TILE_DATA_END);
        /* log_ppu("offset = %x; bitplane = %d\n", offset, ppu->vram[offset]); */

        ppu->bg_fetcher.bitplane1 = ppu->vram[offset];
        ppu->bg_fetcher.state = PUSH;
}

static void load_bg_fifo(struct ppu *ppu)
{
      if (ppu->bg_fifo.len == 0) {
                struct fifo_entry e;
                for (int bit = 7; bit >= 0; bit--) {
                        u8 low    = ((ppu->bg_fetcher.bitplane0 >> bit) & 1);
                        u8 high   = ((ppu->bg_fetcher.bitplane1 >> bit) & 1);
                        e.color   = (u8)(low | high << 1);
                        e.palette = 4; /* NOTE! */
                        push_to_fifo(&ppu->bg_fifo, e);
                }
                assert(ppu->bg_fifo.len == 8);
      }
}

static void tick_drawing(struct ppu *ppu, u8 *interrupt_flag);

static void clock_fifos(struct ppu *ppu)
{
        /* TODO: sprites */
        if (!ppu->shift_counter_enabled || ppu->bg_fifo.len == 0) {
                return;
        }

        struct fifo_entry bg = pop_fifo(&ppu->bg_fifo);

        /* if (ppu->shift_counter_enabled) */
        ppu->shift_count++;
        if (ppu->pixel_counter_enabled)
                ppu->pixel_count++;

        if (ppu->pixel_count < 8) {
                log_ppu("did pop but less now");
                return;
        }

        int pos = ppu->ly * 160 + ppu->lx;
        ppu->display_buf[pos] = ppu->palette[bg.color];
        ppu->lx++;
        /* printf("now lx is %d\n", ppu->lx); */
        log_ppu("displayed");
}


static void new_drawing(struct ppu *ppu)
{
        assert(ppu->active_fetcher == BG_FETCHER);

        if (!ppu->in_first_fetch) {
                struct fifo_entry bg = pop_fifo(&ppu->bg_fifo);

                /* fine horizontal scrolling */
                if (ppu->shift_count++ >= (ppu->scx & 7))
                        ppu->initial_pixels_dropped = true;

                if (ppu->initial_pixels_dropped) {
                        /* delay for dummy fetch */
                        if (ppu->initial_delay < 8) {
                                ppu->initial_delay++;
                        } else {
                                /* actually push to the LCD */
                                u8 color = (ppu->bgp >> (bg.color * 2)) & 0x3;
                                /* BG disabled */
                                if ((ppu->lcdc & 0x1) == 0)
                                        color = 0;
                                u32 gui_color = ppu->palette[color];
                                int pos = ppu->ly * 160 + ppu->lx;
                                ppu->display_buf[pos] = gui_color;
                                if (++ppu->lx == 160) {
                                        switch_to_mode(ppu, HBLANK);
                                        return;
                                }
                        }
                }
        }

        

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
                if (ppu->in_first_fetch) {
                        assert(ppu->bg_fifo.len == 0);
                        load_bg_fifo(ppu);

                        ppu->in_first_fetch = false;
                        ppu->bg_fetcher.state = FETCH_TILE_ID_IDLE;
                } else {
                        ppu->bg_fetcher.state = PUSH;
                }
                break;
        case PUSH:
                if (ppu->bg_fifo.len == 0) {
                        load_bg_fifo(ppu);
                        ppu->bg_fetcher.state = FETCH_TILE_ID_IDLE;
                } else {
                        /* will try again next time */
                }
                break;
        }
}

static void pop_bg_fifo(struct ppu *ppu)
{
        
}

static void try_clocking_fifos(struct ppu *ppu)
{
        if (!ppu->shift_counter_enabled)
                return;
        struct fifo_entry bg = pop_fifo(&ppu->bg_fifo);
        if (ppu->pixel_count >= 8) {
                u8 color = (ppu->bgp >> (bg.color * 2)) & 0x3;
                if ((ppu->lcdc & 1) == 0) {
                        color = 0;
                }
                u32 display_color = ppu->palette[color];
                ppu->display_buf[ppu->ly * 160 + ppu->lx] = display_color;
                log_ppu("puxhed pxiel");
                ppu->lx++;
        }
        ppu->shift_count++;
        if (ppu->pixel_counter_enabled) {
                ppu->pixel_count++;
        }
}

static struct obj * sprite_encountered(struct ppu *ppu)
{
        for (int i = 0; i < ppu->nslots; i++) {
                if (ppu->obj_slots[i].x == ppu->pixel_count)
                        return &ppu->obj_slots[i];
        }
        return NULL;
}

static void tick_drawing(struct ppu *ppu, u8 *interrupt_flag)
{
        if (ppu->shift_count == (ppu->scx & 0x7))
                ppu->pixel_counter_enabled = true;

        if (sprite_encountered(ppu)) {
                ppu->finishing_bg_fetch = true;
                log_ppu("sprite encountered");
        }

        try_clocking_fifos(ppu);

        bool reset_shift_counter = false;
        switch (ppu->bg_fetcher.state) {
        case FETCH_TILE_ID_IDLE:
                log_ppu("fetch_tile_id_idle");
                ppu->bg_fetcher.state = FETCH_TILE_ID;

                break;
        case FETCH_TILE_ID:
                log_ppu("fetch_tile_id");
                fetch_bg_tile_id(ppu);
                ppu->bg_fetcher.state = FETCH_BITPLANE0_IDLE;
                break;
        case FETCH_BITPLANE0_IDLE:
                log_ppu("fetch_bitplane0_idle");
                ppu->bg_fetcher.state = FETCH_BITPLANE0;
                break;
        case FETCH_BITPLANE0:
                log_ppu("fetch_bitplane0");
                fetch_bg_bitplane0(ppu);
                ppu->bg_fetcher.state = FETCH_BITPLANE1_IDLE;
                break;
        case FETCH_BITPLANE1_IDLE:
                log_ppu("fetch_bitplane1_idle");
                ppu->bg_fetcher.state = FETCH_BITPLANE1;
                break;
        case FETCH_BITPLANE1:
                log_ppu("fetch_bitplane1");
                fetch_bg_bitplane1(ppu);

                if (ppu->shift_counter_enabled) {
                        ppu->bg_fetcher.state = PUSH;
                        /* printf("here!"); */
                        /* if (ppu->finishing_bg_fetch) { */
                        /*         Q; */
                        /* } */
                } else {
                        load_bg_fifo(ppu);
                        ppu->bg_fetcher.state = FETCH_TILE_ID_IDLE;
                        ppu->shift_counter_enabled = true;
                        log_ppu("enabled shift counter");
                }
                break;
        case PUSH:

                if (ppu->bg_fifo.len == 0) {
                        log_ppu("push worked immedietely");
                        load_bg_fifo(ppu);
                        ppu->bg_fetcher.state = FETCH_TILE_ID_IDLE;
                        ppu->shift_count = 0;
                        reset_shift_counter = true;
                } else{
                        log_ppu("couldn't push");
                }
                break;
        }

        /* if (enable_shift_counter) */
        /*         ppu->shift_counter_enabled = true; */
        if (reset_shift_counter)
                ppu->shift_count = 0;
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
                                ppu->oam_access_blocked = true;
                        if (!ppu->dma_in_progress)
                                return;
                }
        }

        assert(ppu->dma_offset >= 0 && ppu->dma_offset < 160);

        u16 addr = (ppu->dma << 8) + ppu->dma_offset;
        ppu->oam[ppu->dma_offset++] = read_mem(ppu->mem, addr);

        if (ppu->dma_offset == 160)
                ppu->dma_in_progress = ppu->oam_access_blocked = false;
}

static bool vram_accessible(enum ppu_mode m)
{
        return true; /* TODO */
        /* return false; */
        /* return m != DRAWING; */
}

static void write_vram(struct ppu *ppu, u8 v, u16 addr)
{
        /* if (vram_accessible(ppu->mode)) */
                ppu->vram[addr - VRAM_START] = v;
}

static u8 read_vram(struct ppu *ppu, u16 addr)
{
        /* if (!vram_accessible(ppu->mode)) */
                /* return 0xFF; /\* garbage read *\/ */
        return ppu->vram[addr - VRAM_START];
}

static bool oam_accessible(struct ppu *ppu)
{
        if (ppu->oam_access_blocked)
                return false;
        /* if (ppu->cycles_since_dma_initiated <= 161) */
        /*         return false; */
        /* return true; /\* TODO *\/ */
        /* return m == HBLANK || m == VBLANK; */
        return true;
}

static void write_oam(struct ppu *ppu, u8 v, u16 addr)
{
        /* printf("OAM: wrote %d to $%x on %llx\n", v, addr, *TICK); */
        if (oam_accessible(ppu))
                ppu->oam[addr - OAM_START] = v;
}

static u8 read_oam(struct ppu *ppu, u16 addr)
{
        u8 v = oam_accessible(ppu) ? ppu->oam[addr - OAM_START] : 0xFF;
        if (oam_accessible(ppu)) {
                dbg("read allowed, gave %d\n", v);
        } else {
                if (ppu->lcd_reenabled)
                        return ppu->oam[addr - OAM_START];
                dbg("read blocked, gave %d\n", v);
        }
        return v;
}

static void sync_ppu(struct ppu *ppu, u8 *interrupt_flag)
{
        oam_dma(ppu);

        if (!(ppu->lcdc >> 7)) {
                /* printf("ppu disabled\n"); */
                ppu->ly = 0;
                ppu->stat &= ~0x3;
                ppu->line_delta = 0;
                return;
        }

        switch (ppu->mode) {
        case OAM_SCAN:
                oam_scan(ppu, interrupt_flag);
                break;
        case DRAWING:;
                /* drawing(ppu, interrupt_flag); */
                break;
        case HBLANK:
                hblank(ppu, interrupt_flag);
                break;
        case VBLANK:
                vblank(ppu, interrupt_flag);
                break;
        }
}


static bool stat_interrupt_pending(struct ppu *ppu, u8 *interrupt_flag)
{
        bool stat_line = false;
        int statbit=0;
        char *reason = "none";
        if (read_ppu_reg(ppu, LY_ADDR) == ppu->lyc && (ppu->stat & (1 << 6))) {
                statbit = 6;
                reason = "lyc";
                /* assert(ppu->ly != ppu->prev_stat_ly); */
                /* printf("fired interrupt for bit %d of stat %d\n", 6, *TICK); */
                stat_line = true;
        } else if (ppu->mode == 2 && (ppu->stat & (1 << 5))) {
                statbit = 5;
                reason = "mode2";

                stat_line = true;
        } else if (ppu->mode == 1 && (ppu->stat & (1 << 4))) {
                statbit = 4;
                reason = "mode1";
                /* printf("fired interrupt for bit %d of stat %d\n", 4, *TICK); */
                stat_line = true;
        } else if (ppu->mode == 0 && (ppu->stat & (1 << 3))) {
                reason = "mode0";
                statbit = 1;
                /* printf("fired interrupt for bit %d of stat %d\n", 4, *TICK); */
                stat_line = true;
        }

        bool interrupt_pending = stat_line && !ppu->prev_stat_line;
        /* if (stat_line && !ppu->prev_stat_line) { */
        /* printf("stat interrupt (t=%lld, delta=%d, frame = %d, statbit = %d, ly is %d, reason=%s)\n", */
        /*        *TICK, ppu->line_delta, *FRAME, statbit, ppu->ly, reason); */

        if (interrupt_pending && statbit == 5)
                printf("fired interrupt for bit %d of stat on frame %ld tick %ld line delta %d\n", 5, *FRAME, *TICK, ppu->line_delta);
        ppu->prev_stat_line = stat_line;
        /* ppu->prev_stat_ly   = ppu->ly; */
        return interrupt_pending;
}

static void update_coincidence_flag(struct ppu *ppu)
{
        if (read_ppu_reg(ppu, LY_ADDR) == ppu->lyc)
                ppu->stat |= (1 << 2);
        else
                ppu->stat &= ~(1 << 2);
}

static void step_dot(struct ppu *ppu, u8 *interrupt_flag)
{
        update_coincidence_flag(ppu);
        if (stat_interrupt_pending(ppu, interrupt_flag)) {
                *interrupt_flag |= 1 << 1;
                /* printf("requested interrupt on tick %llu frame %d ly is %d!, line delta is %d\n", *TICK, *FRAME, ppu->ly, ppu->line_delta); */
        }
        ppu->line_delta++;
        update_mode(ppu);
}

static void write_ppu_reg(struct ppu *ppu, u8 v, u16 addr)
{
        switch(addr) {
        case LCDC_ADDR:
                log_ppu("lcdc_addr");
                if (!(ppu->lcdc >> 7) && (v >> 7)) {
                        ppu->lcd_reenabled = true;
                        ppu->mode = OAM_SCAN;
                }
                /* if (!((ppu->lcdc >> 7) & 0x1) && ((v >> 7) & */
                /* ppu->disabled = !((v >> 7) & 0x1); */
                /* printf("LCD turned off\n"); */
                /* else */
                /* printf("LCD turned on\n"); */
                ppu->lcdc = v;
                break;
        case STAT_ADDR:
                ppu->stat = (v & ~0x7) | (ppu->stat & 0x7);
                ppu->stat |= (1 << 7);
                break;
        case SCY_ADDR:
                ppu->scy = v;
                break;
        case SCX_ADDR:
                ppu->scx = v;
                break;
        case LY_ADDR:
                ppu->ly = v;
                break;
        case LYC_ADDR:
                /* printf("wrote LYC = %d = 0x%x on tick %llu frame %d (ly is %d, line_delta is %d)\n", v, v, *TICK, *FRAME, ppu->ly, ppu->line_delta); */
                ppu->lyc = v;
                update_coincidence_flag(ppu);
                break;
        case DMA_ADDR:
                assert(v <= 0xDF); /* TODO: check what happens here? */
                /* if (oam_accessible(ppu)) { */
                ppu->dma = v;
                ppu->dma_requested= true;
                ppu->cycles_since_dma_requested = 0;
                dbg("wrote %d to dma (ff46)\n",
                    v);
                /* } else { */
                /*         dbg("skipped write  %d to dma (ff46)\n", v); */
                /* } */
                break;
        case BGP_ADDR:
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
                /* if (ppu->ly == 153 && ppu->line_delta >= 4) { */
                /*         /\*         printf("Subbed ly for 0 due to ly == 153\n"); *\/ */
                /*         return 0; */
                /* } */
                /* return 0x90; */
                return ppu->ly;
        case LYC_ADDR:
                return ppu->lyc;
        case DMA_ADDR:
                /* printf("read dma addr %d\n", addr); */
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
