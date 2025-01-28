#define pf(...) do {printf("tick = %ld; dot = %d;", *TICK, ppu->dots_since_scanline_started);            \
                    printf(__VA_ARGS__); putchar('\n');}  while (0)

static void switch_to_mode(struct ppu *ppu, enum ppu_mode m)
{
        /* pf("switched to mode %d now", m); */
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

static void oam_dma(struct ppu *ppu);

static void new_oam_scan(struct ppu *ppu);
static void new_drawing(struct ppu *ppu);
static void new_hblank(struct ppu *ppu, u8 *interrupt_flag);
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
                new_hblank(ppu, interrupt_flag);
                break;
        case VBLANK:
                new_vblank(ppu);
                break;
        }

        if (stat_interrupt_pending(ppu, interrupt_flag)) {
                *interrupt_flag |= 1 << 1;
        }

        ppu->dots_since_scanline_started++;
        ppu->dots_since_frame_started++;
}


static void new_hblank(struct ppu *ppu, u8 *interrupt_flag)
{
        /* TODO: weird ly stuff */
        if (ppu->dots_since_scanline_started == 455) {
                /* pf("here"); */
                ppu->ly++;
                if (ppu->ly == 144) {
                        switch_to_mode(ppu, VBLANK);
                        *interrupt_flag |= (1 << 1);
                } else {
                        switch_to_mode(ppu, OAM_SCAN);
                }
                ppu->dots_since_scanline_started = -1;
        }
}

static void new_vblank(struct ppu *ppu)
{
        if (ppu->dots_since_scanline_started == 455) {
                ppu->ly++;
                if (ppu->ly == 154) {
                        ppu->ly = 0;
                        switch_to_mode(ppu, OAM_SCAN);
                        assert(ppu->dots_since_frame_started == 70223);
                        ppu->dots_since_frame_started = -1;
                } else {
                        /* nothing */
                }
                ppu->dots_since_scanline_started = -1;
        }
}

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
                ppu->lx = 0;
                /* printf("Reset lx!\n"); */

                ppu->active_fetcher = BG_FETCHER;
                ppu->initial_fetch_completed = false;
                ppu->scx_pixels_dropped = false;
                ppu->initial_delay = 0;
                memset(&ppu->obj_fifo, 0, sizeof ppu->obj_fifo);
                memset(&ppu->bg_fifo, 0, sizeof ppu->bg_fifo);
                memset(&ppu->obj_fetcher, 0, sizeof ppu->obj_fetcher);
                memset(&ppu->bg_fetcher, 0, sizeof ppu->bg_fetcher);

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
        u8 n = (ppu->lcdc >> 3) & 1;
        u8 x = (ppu->scx + ppu->pixel_count) / 8;
        u8 y = (ppu->ly + ppu->scy) & 0xF8;

        u16 offset = 0x1800 | n << 10 | y << 2 | x;

        assert(x <= 0x3F);
        assert(vram_offset_to_addr(offset) >= TILEMAP1_START &&
               vram_offset_to_addr(offset) <= TILEMAP2_END);

        ppu->bg_fetcher.tile_id = ppu->vram[offset];
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

        ppu->bg_fetcher.bitplane0 = ppu->vram[offset];
}

static void fetch_bg_bitplane1(struct ppu *ppu)
{
        u8 tile_id = ppu->bg_fetcher.tile_id;
        u16 offset = 1 + bitplane_formula(ppu, tile_id);

        assert(vram_offset_to_addr(offset) >= TILE_DATA_START &&
               vram_offset_to_addr(offset) <= TILE_DATA_END);

        ppu->bg_fetcher.bitplane1 = ppu->vram[offset];
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

static void new_drawing(struct ppu *ppu)
{
        assert(ppu->active_fetcher == BG_FETCHER);

        if (ppu->initial_fetch_completed) {
                assert(ppu->dots_since_scanline_started >= 86);

                struct fifo_entry bg = pop_fifo(&ppu->bg_fifo);

                if (!ppu->scx_pixels_dropped &&
                    ppu->shift_count >= (ppu->scx & 0x7))
                        ppu->scx_pixels_dropped = true;

                /* fine horizontal scrolling */
                if (ppu->scx_pixels_dropped) {
                        /* actually push to the LCD */
                        if (ppu->pixel_count >= 8) {
                                u8 color = (ppu->lcdc & 0x1) ? bg.color : 0;

                                /* color = ppu->dots_since_scanline_started % 2; */
                                u32 gui_color = ppu->palette[color];
                                int pos = ppu->ly * 160 + ppu->lx;
                                ppu->display_buf[pos] = gui_color;
                                if (++ppu->lx == 160) {
                                        switch_to_mode(ppu, HBLANK);
                                        return;
                                }
                        }
                        ppu->pixel_count++;
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
                if (!ppu->initial_fetch_completed) {
                        load_bg_fifo(ppu);

                        ppu->initial_fetch_completed = true;
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
        /* if (oam_accessible(ppu)) { */
        /* } else { */
        /*         /\* if (ppu->lcd_reenabled) *\/ */
        /*         /\*         return ppu->oam[addr - OAM_START]; *\/ */
        /* } */
        return v;
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
                /* if (!(ppu->lcdc >> 7) && (v >> 7)) { */
                /*         ppu->lcd_reenabled = true; */
                /*         ppu->mode = OAM_SCAN; */
                /* } */
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
                printf("Yep, DMA was written to on cycle %ld!\n", *TICK);
                assert(v <= 0xDF); /* TODO: check what happens here? */
                ppu->dma = v;
                ppu->dma_requested= true;
                ppu->cycles_since_dma_requested = 0;
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
