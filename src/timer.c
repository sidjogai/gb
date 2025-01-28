static void sync_timers(struct timer *t, tick cur_tick, u8 *interrupt_flag)
{
        /* https://github.com/Hacktix/GBEDG/blob/master/timers/index.md */

        bool div_bit, tima_enable_bit, prev, cur, falling_edge;

        static const u32 bits[] = {9, 3, 5, 7};

        for (int i = 0; i < 4; i++) {
                prev = t->prev_result;

                t->div++;
 
                div_bit         = (t->div >> bits[t->tac & 0x3]) & 1;
                tima_enable_bit = (t->tac >> 2) & 1;
                cur             = div_bit && tima_enable_bit;
                falling_edge    = prev == 1 && cur == 0;
                t->prev_result  = cur;

                if (falling_edge) {
                        assert(cur_tick > t->irq.tick_due + 4);

                        t->tima++;

                        /* irq due; process it after four cycles... */
                        if (t->tima == 0) {
                                assert(i == 3);
                                t->irq.is_pending = true;
                                t->irq.tick_due = cur_tick + 4;
                        }
                }

                assert(t->tima == 0 ||
                       !t->irq.is_pending ||
                       t->irq.is_pending && cur_tick > t->irq.tick_due + 4);

                /* ...now */
                if (t->irq.is_pending && cur_tick > t->irq.tick_due) {
                        t->irq.is_pending = false;
                        t->tima = t->tma;
                        *interrupt_flag = (1U << 2);
                }
        }
}

static void write_div(struct timer *t, u8 v)
{
        t->div = 0;
}

static u8 read_div(struct timer *t)
{
        return t->div >> 8;
}

static void write_tima(struct timer *t, u8 v, tick cur_tick)
{
        /* tima written in between irq scheduled and irq fired; cancel the
           irq */
        if (t->irq.is_pending && cur_tick <= t->irq.tick_due + 4) {
                /* die("tima write during the same cycle on tick %lld: check
                   this behaviour\n", cur_tick); */
                t->irq.is_pending = false;
                t->tima = v;
        } else {
                t->tima = v;
        }
}

static u8 read_tima(struct timer *t)
{
        return t->tima;
}

static void write_tma(struct timer *t, u8 v)
{
        t->tma = v;
}

static u8 read_tma(struct timer *t)
{
        return t->tma;
}

static void write_tac(struct timer *t, u8 v)
{
        t->tac = v;
}

static u8 read_tac(struct timer *t)
{
        return t->tac;
}
