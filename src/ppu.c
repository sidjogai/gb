

/* static bool ppu_loggin_enabled = true; */
static bool ppu_loggin_enabled = false;

#ifdef GB_LOG_PPU
#define log_ppu(...)                                    \
        do {                                            \
                if (!ppu_loggin_enabled)                \
                        break;                          \
                printf("mode = %-8s; "                  \
                       "delta = %-3d; "                 \
                       "ly = %-3d; "                    \
                       "lx = %-3d; "                    \
                       "pixelcount = %-3d"              \
                       "        %s:%-3d:%-22s        ", \
                       ppu_mode_name(ppu->mode),        \
                       ppu->line_delta,                 \
                       ppu->ly,                         \
                       ppu->lx,                         \
                       ppu->new.pixelcount,             \
                       &__FILE__[2],                    \
                       __LINE__,                        \
                       __func__                         \
                       );                               \
                fprintf(stdout, __VA_ARGS__);           \
        } while(0)
#else
#define log_ppu(...) ;
#endif
/* static bool valid_fetcher_mode(enum fetcher_mode m) */
/* { */
/*         return m == BG_FETCHER || m == OBJ_FETCHER; */
/* } */

static bool valid_ppu_mode(enum ppu_mode m)
{
        return m == HBLANK || m == VBLANK || m == OAM_SCAN || m == DRAWING;
}

static const char * ppu_mode_name(enum ppu_mode m)
{
        assert(valid_ppu_mode(m));
        static const char * const names[] = {
                [OAM_SCAN] = "oam scan",
                [DRAWING]  = "drawing",
                [HBLANK]   = "hblank",
                [VBLANK]   = "vblank",
        };
        return names[m];
}

static u16 vram_offset_to_addr(u16 offset)
{
        return 0x8000 + offset;
}

#define valid_bg_fetcher_mode(m) ((m) >= 0 && (m) < 2)

static bool oam_scan_invariant(struct ppu *ppu)
{
        assert(ppu->mode == OAM_SCAN);
        assert(ppu->line_delta >= 0 && ppu->line_delta <= 80);
        /* assert(ppu->lx == 0); */
        assert(ppu->ly >= 0 && ppu->ly <= 143);
        return true;
}

static void trigger_stat_interrupt(struct ppu *ppu, int bit, u8 *interrupt_flag)
{
        printf("Fired it on tick %d!\n", ppu->line_delta);
        *interrupt_flag |= (1 << 1);
}

static void switch_to_mode(struct ppu *ppu, enum ppu_mode m, u8 *interrupt_flag)
{
        assert(valid_ppu_mode(m));

        switch(m) {
        case OAM_SCAN:
                ppu->mode       = OAM_SCAN;
                ppu->line_delta = 0;
                ppu->nslots     = 0;

                ppu->lx         = 0;
                break;
        case DRAWING:
                ppu->mode = DRAWING;
                ppu->active_fetcher = BG_FETCHER;
                memset(&ppu->new, 0, sizeof ppu->new);
                memset(&ppu->obj_fifo, 0, sizeof ppu->obj_fifo);
                memset(&ppu->bg_fifo, 0, sizeof ppu->bg_fifo);
                memset(&ppu->obj_fetcher, 0, sizeof ppu->obj_fetcher);
                memset(&ppu->bg_fetcher, 0, sizeof ppu->bg_fetcher);
                break;
        case VBLANK:
                ppu->mode       = VBLANK;
                ppu->line_delta = 0;

                ppu->ly         = 144;
                break;
        case HBLANK:
                ppu->mode = HBLANK;
                break; /* TODO: fill this!*/
                /* Q; */
        }

        ppu->stat &= ~0x7;
        ppu->stat |= ppu->mode;
        ppu->stat |= (ppu->ly == ppu->scy) << 2;

        log_ppu("Switched to mode %s\n",
                ppu_mode_name(ppu->mode));

        for (int bit = 3; bit <= 5; bit++)
                if (ppu->stat & (1 << bit))
                        trigger_stat_interrupt(ppu, bit, interrupt_flag);
}


static void vblank(struct ppu *ppu, u8 *interrupt_flag);
static void drawing(struct ppu *ppu, u8 *interrupt_flag);

static void oam_scan(struct ppu *ppu, u8 *interrupt_flag)
{
        if ((ppu->line_delta += 4) != 80)
                return;

        /* TODO: the sprite with the lowest x position "wins" */
        ppu->nslots = 0;

        u8 sprite_height = (ppu->lcdc & (1 << 2)) ? 16 : 8;
        if (sprite_height == 16)
                Q;

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
        switch_to_mode(ppu, DRAWING, interrupt_flag);
}

static void hblank(struct ppu *ppu, u8 *interrupt_flag)
{
        if ((ppu->line_delta += 4) < 456)
                return;

        if (ppu->ly == 143)
                switch_to_mode(ppu, VBLANK, interrupt_flag);
        else {
                switch_to_mode(ppu, OAM_SCAN, interrupt_flag);
                if (++ppu->ly == ppu->lyc) {
                        trigger_stat_interrupt(ppu, 6, interrupt_flag);
                }
        }
}

static void oam_dma(struct ppu *ppu);

static void sync_ppu(struct ppu *ppu, u8 *interrupt_flag)
{
        switch (ppu->mode) {
        case OAM_SCAN:
                /* assert(oam_scan_invariant(ppu)); */
                oam_scan(ppu, interrupt_flag);
                break;
        case DRAWING:;
                /* assert(drawing_invariant(ppu)); */
                drawing(ppu, interrupt_flag);
                oam_dma(ppu); /* TODO: check this */
                break;
        case HBLANK:
                /* assert(hblank_invariant(ppu)); */
                hblank(ppu, interrupt_flag);
                break;
        case VBLANK:
                /* assert(vblank_invariant(ppu)); */
                vblank(ppu, interrupt_flag);
                break;
        }

        oam_dma(ppu);
}

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

static void clock_fifos(struct ppu *ppu)
{
        if (ppu->new.obj_fetch_underway)
                return;
        
        struct fifo_entry bg = pop_fifo(&ppu->bg_fifo);

        if (ppu->obj_fifo.len > 0) {
                bg = pop_fifo(&ppu->obj_fifo);
                /* Q; */
        }

        if (ppu->new.pixelcount < 8)
                log_ppu("popped; pixelcount = %d; bg_fifo_len = %d\n",
                        ppu->new.pixelcount, ppu->bg_fifo.len);

        if (ppu->new.pixelcount >= 8 && ppu->new.pixelcount <= 167) {
                int pos = ppu->ly * 160 + ppu->lx;
                ppu->display_buf[pos] = ppu->palette[bg.color];
                ppu->lx++;
                log_ppu("displayed; pixelcount = %d; bg_fifo_len = %d\n",
                        ppu->new.pixelcount, ppu->bg_fifo.len);
        }

        ppu->new.pixelcount++;
}

static void fetch_bg_tile_id(struct ppu *ppu)
{
        u8 n = (ppu->lcdc >> 3) & 1;
        u8 x = ppu->scx + (ppu->new.pixelcount / 8);
        u8 y = (ppu->ly + ppu->scy) & 0xF8;

        u16 offset = 0x1800 | n << 10 | y << 2 | x;

        assert(x <= 0x3F);
        assert(vram_offset_to_addr(offset) >= TILEMAP1_START &&
               vram_offset_to_addr(offset) <= TILEMAP2_END);

        log_ppu("fetch #%d; offset = %x; tile_no = %d\n",
                ppu->new.nfetch++, offset, ppu->obj_fetcher.tile_id);

        ppu->bg_fetcher.tile_id = ppu->vram[offset];
        ppu->bg_fetcher.state = FETCH_BITPLANE0;
}

static void fetch_obj_tile_id(struct ppu *ppu)
{
        assert(ppu->cur_obj != NULL);

        log_ppu("tile number was %d\n", ppu->cur_obj->tile_index);


        ppu->obj_fetcher.tile_id = ppu->cur_obj->tile_index;
        ppu->obj_fetcher.state = FETCH_BITPLANE0;
}

static void fetch_bg_bitplane0(struct ppu *ppu)
{
        u8 tile_id = ppu->bg_fetcher.tile_id;
        u16 offset = (ppu->lcdc & (1 << 4)) ?
                (tile_id << 4) | (((ppu->ly + ppu->scy) & 0x7) << 1) :
                (0x1000 - (tile_id << 4)) | (((ppu->ly + ppu->scy) & 0x7) << 1);

        assert(vram_offset_to_addr(offset) >= TILE_DATA_START &&
               vram_offset_to_addr(offset) <= TILE_DATA_END);
        log_ppu("offset = %x; bitplane = %d\n", offset, ppu->vram[offset]);

        ppu->bg_fetcher.bitplane0 = ppu->vram[offset];
        ppu->bg_fetcher.state = FETCH_BITPLANE1;
}

static void fetch_obj_bitplane0(struct ppu *ppu)
{
        /* TODO: attributes, etc. */
        u8 tile_id = ppu->obj_fetcher.tile_id;
        u16 offset = (ppu->lcdc & (1 << 4)) ?
                (tile_id << 4) | (((ppu->ly + ppu->scy) & 0x7) << 1) :
                (0x1000 - (tile_id << 4)) | (((ppu->ly + ppu->scy) & 0x7) << 1);

        assert(vram_offset_to_addr(offset) >= TILE_DATA_START &&
               vram_offset_to_addr(offset) <= TILE_DATA_END);
        log_ppu("offset = %x; bitplane = %d\n", offset, ppu->vram[offset]);

        ppu->obj_fetcher.bitplane0 = ppu->vram[offset];
        ppu->obj_fetcher.state = FETCH_BITPLANE1;
}

static void fetch_bg_bitplane1(struct ppu *ppu)
{
        u8 tile_id = ppu->bg_fetcher.tile_id;
        u16 offset = 1;
        offset |= (ppu->lcdc & (1 << 4)) ?
                (tile_id << 4) | (((ppu->ly + ppu->scy) & 0x7) << 1) :
                (0x1000 - (tile_id << 4)) | (((ppu->ly + ppu->scy) & 0x7) << 1);

        assert(vram_offset_to_addr(offset) >= TILE_DATA_START &&
               vram_offset_to_addr(offset) <= TILE_DATA_END);
        log_ppu("offset = %x; bitplane = %d\n", offset, ppu->vram[offset]);

        ppu->bg_fetcher.bitplane1 = ppu->vram[offset];
        ppu->bg_fetcher.state = PUSH;
}

static void fetch_obj_bitplane1(struct ppu *ppu)
{
        /* TODO: attributes, etc. */
        u8 tile_id = ppu->obj_fetcher.tile_id;
        u16 offset = 1;
        offset |= (ppu->lcdc & (1 << 4)) ?
                (tile_id << 4) | (((ppu->ly + ppu->scy) & 0x7) << 1) :
                (0x1000 - (tile_id << 4)) | (((ppu->ly + ppu->scy) & 0x7) << 1);

        assert(vram_offset_to_addr(offset) >= TILE_DATA_START &&
               vram_offset_to_addr(offset) <= TILE_DATA_END);
        log_ppu("offset = %x; bitplane = %d\n", offset, ppu->vram[offset]);

        ppu->obj_fetcher.bitplane1 = ppu->vram[offset];
        ppu->obj_fetcher.state = PUSH;
}

static void push_to_bg_fifo(struct ppu *ppu)
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
                log_ppu("pushed 8 pixels\n");
                assert(ppu->bg_fifo.len = 8);

                if (ppu->new.obj_fetch_underway) {
                        ppu->obj_fetcher.state = FETCH_TILE_ID;
                        ppu->bg_fetcher.state  = FETCH_TILE_ID;
                        ppu->active_fetcher    = OBJ_FETCHER;
                        ppu_loggin_enabled     = true;
                } else {
                        ppu->bg_fetcher.state = FETCH_TILE_ID;
                }

        } else if (ppu->new.obj_fetch_underway) {
                /* NOTE: */
                ppu->active_fetcher    = OBJ_FETCHER;
                ppu->bg_fetcher.state  = PUSH;
                ppu->obj_fetcher.state = FETCH_TILE_ID;
                ppu_loggin_enabled     = true;
        }

}

static void push_to_obj_fifo(struct ppu *ppu)
{
        /* TODO: pixel mixing! */
        ppu->obj_fifo.len = 0;
        struct fifo_entry e;
        for (int bit = 7; bit >= 0; bit--) {
                u8 low    = ((ppu->obj_fetcher.bitplane0 >> bit) & 1);
                u8 high   = ((ppu->obj_fetcher.bitplane1 >> bit) & 1);
                e.color   = (u8)(low | high << 1);
                e.palette = 4; /* NOTE! */
                push_to_fifo(&ppu->obj_fifo, e);
        }
        ppu->obj_fetcher.state = FETCH_TILE_ID;
        ppu->active_fetcher = BG_FETCHER;
        ppu->new.obj_fetch_underway = false;
}

static struct obj * sprite_hit(struct ppu *ppu)
{
        struct obj *p = ppu->obj_slots;
        /* TODO: bounds */
        for (struct obj *obj = p; obj < p + ppu->nslots; obj++)
                if (obj->x >= ppu->lx && obj->x <= ppu->lx + 8) {
                        if (!obj->seen) {
                                obj->seen = true;
                                /* printf("obj with x=%d, y=%d not seen, adding!\n",  */
                                /*        obj->x, obj->y); */

                                return obj;
                        } else {
                                /* printf("skipping obj with x=%d, y=%d as it is seen\n",  */
                                /*        obj->x, obj->y); */
                        }
                }
        return NULL;
}

#define CLOCK(n)                                                        \
        for (int i = 0; i < n; i++) {                                   \
                clock_fifos(ppu);                                       \
                if ((ppu->cur_obj = sprite_hit(ppu)) != NULL)           \
                        ppu->new.obj_fetch_underway = true;          \
                if (ppu->lx >= 160)                                     \
                        goto hblank;                                    \
        }

static int fetcher_state_duration(enum fetcher_state s)
{
        return s == PUSH ? 1 : 2;
}

static void drawing(struct ppu *ppu, u8 *interrupt_flag)
{
#define tick(n) do { ppu->line_delta += n; left -= n; } while (0)

        /* assert(ppu->line_delta % 4 == 0); */

        int d    = ppu->line_delta - 80;
        int left = 4 + ppu->new.q;
        ppu->new.q = 0;

        if (d == 0) {
                /* B */
                fetch_bg_tile_id(ppu);
                ppu->line_delta += 2;

                /* 0 */
                fetch_bg_bitplane0(ppu);
                ppu->line_delta += 2;
                return;
        } else if (d == 4) {
                /* 1 */
                fetch_bg_bitplane1(ppu);
                push_to_bg_fifo(ppu); /* FIFO empty; instant push */
                tick(2);
                
                ppu->bg_fetcher.state = FETCH_TILE_ID;
        }

 start:;
        struct fetcher *active_fetcher = ppu->active_fetcher == BG_FETCHER ?
                &ppu->bg_fetcher : &ppu->obj_fetcher;
        if (left < fetcher_state_duration(active_fetcher->state))
                goto done;

        if (ppu->active_fetcher == BG_FETCHER)
                switch (active_fetcher->state) {
                case FETCH_TILE_ID:
                        fetch_bg_tile_id(ppu);
                        CLOCK(2);
                        tick(2);
                        break;
                case FETCH_BITPLANE0:
                        fetch_bg_bitplane0(ppu);
                        CLOCK(2);
                        tick(2);
                        break;
                case FETCH_BITPLANE1:
                        fetch_bg_bitplane1(ppu);
                        CLOCK(2);
                        tick(2);
                        break;
                case PUSH:
                        CLOCK(1);
                        tick(1);

                        push_to_bg_fifo(ppu); /* kind of the wrong order */

                        break;
                }
        else {
                /* Q;   */
                switch (active_fetcher->state) {
                case FETCH_TILE_ID:
                        fetch_obj_tile_id(ppu);
                        tick(2);

                        break;
                case FETCH_BITPLANE0:
                        fetch_obj_bitplane0(ppu);
                        tick(2);

                        break;
                case FETCH_BITPLANE1:
                        fetch_obj_bitplane1(ppu);
                        tick(2);

                        break;
                case PUSH:
                        tick(1);

                        push_to_obj_fifo(ppu);
                        break;
                }
        }

        goto start;
        return;

 hblank:
        switch_to_mode(ppu, HBLANK, interrupt_flag);

        return;

 done:
        if (left != 0) {
                ppu->new.q = left;
                tick(left);
        }
        return;
}

static void vblank(struct ppu *ppu, u8 *interrupt_flag)
{
        if ((ppu->line_delta += 4) != 456)
                return;

        ppu->line_delta = 0;
        if (++ppu->ly == 154) {
                switch_to_mode(ppu, OAM_SCAN, interrupt_flag);
                ppu->ly = 0;
                *interrupt_flag |= 1;
        }

}

static void oam_dma(struct ppu *ppu)
{
        if (!ppu->dma.in_progress)
                return;

        if (ppu->dma.delta > 160) {
                return;
        }

        u16 src_addr = (u16)(ppu->dma.val << 8);
        int offset   = ppu->dma.delta;

        assert(offset >= 0 && offset < len(ppu->oam));

        u8 v = read_mem(ppu->mem, (u16)(src_addr + offset));

        ppu->oam[offset] = v;

        if (++ppu->dma.delta == 160)
                ppu->dma.in_progress = false;
}

static bool vram_accessible(enum ppu_mode m)
{
        return true; /* TODO */
        return m != DRAWING;
}

static bool oam_accessible(enum ppu_mode m)
{
        return true; /* TODO */
        return m == HBLANK || m == VBLANK;
}

static void write_vram(struct ppu *ppu, u8 v, u16 addr)
{
        if (vram_accessible(ppu->mode))
                ppu->vram[addr - VRAM_START] = v;
}

static u8 read_vram(struct ppu *ppu, u16 addr)
{
        if (!vram_accessible(ppu->mode))
                return 0xFF; /* garbage read */
        return ppu->vram[addr - VRAM_START];
}

static void write_oam(struct ppu *ppu, u8 v, u16 addr)
{
        log_ppu("wrote %d to $%x\n", v, addr);
        if (oam_accessible(ppu->mode))
                ppu->oam[addr - OAM_START] = v;
}

static u8 read_oam(struct ppu *ppu, u16 addr)
{
        if (!oam_accessible(ppu->mode))
                return 0xFF; /* garbage read */
        return ppu->oam[addr - OAM_START];
}

static void write_lcdc(struct ppu *ppu, u8 v)
{
        ppu->lcdc = v;
}

static u8 read_lcdc(struct ppu *ppu)
{
        return ppu->lcdc;
}

static void write_stat(struct ppu *ppu, u8 v)
{
        ppu->stat = (v & ~0x7) | (ppu->stat & 0x7);
}

static u8 read_stat(struct ppu *ppu)
{
        return ppu->stat;
}

static void write_scy(struct ppu *ppu, u8 v)
{
        ppu->scy = v;
}

static u8 read_scy(struct ppu *ppu)
{
        return ppu->scy;
}

static void write_scx(struct ppu *ppu, u8 v)
{
        if (v != 0)
                Q;
        ppu->scx = v;
}

static u8 read_scx(struct ppu *ppu)
{
        return ppu->scx;
}

static void write_ly(struct ppu *ppu, u8 v)
{
        /* LY is read-only */
}

static u8 read_ly(struct ppu *ppu)
{
        return ppu->ly;
}

static void write_lyc(struct ppu *ppu, u8 v)
{
        /* Q; */
        ppu->lyc = v;
}

static u8 read_lyc(struct ppu *ppu)
{
        /* Q; */
        return ppu->lyc;
}

static void write_wy(struct ppu *ppu, u8 v)
{
        ppu->wy = v;
}

static u8 read_wy(struct ppu *ppu)
{
        return ppu->wy;
}

static void write_wx(struct ppu *ppu, u8 v)
{
        if (v == 0 || v == 166)
                die("write_wx: set to unreliable value %d\n", v);
        ppu->wx = v;
}

static u8 read_wx(struct ppu *ppu)
{
        return ppu->wx;
}

static void write_dma(struct ppu *ppu, u8 v)
{
        if (v > 0xDF)
                die("TODO: check what to do here\n");

        ppu->dma.in_progress = true;
        ppu->dma.delta       = 0;
        ppu->dma.val         = v;
}

static u8 read_dma(struct ppu *ppu)
{
        return ppu->dma.val;
}

static void write_bgp(struct ppu *ppu, u8 v)
{
        ppu->bgp = v;
}

static u8 read_bgp(struct ppu *ppu)
{
        return ppu->bgp;
}

static void write_obp0(struct ppu *ppu, u8 v)
{
        ppu->obp0 = v;
}

static u8 read_obp0(struct ppu *ppu)
{
        return ppu->obp0;
}
static void write_obp1(struct ppu *ppu, u8 v)
{
        ppu->obp1 = v;
}

static u8 read_obp1(struct ppu *ppu)
{
        return ppu->obp1;
}
