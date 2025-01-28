static void sync_timer(struct timer *t, u8 *interrupt_flag)
{
        t->interrupt_flag = interrupt_flag;
        /* https://github.com/Hacktix/GBEDG/blob/master/timers/index.md */

        bool div_bit, tima_enabled, and_result;

        static const u32 bits[] = {9, 3, 5, 7};

        t->tima_reloaded = false;

        for (int i = 0; i < 4; i++) {
                t->div++;

                div_bit      = (t->div >> bits[t->tac & 0x3]) & 1;
                tima_enabled = (t->tac >> 2) & 1;
                and_result     = div_bit && tima_enabled;

                if (t->prev_and_result == 1 && and_result == 0) {
                        t->tima++;

                        /* after overflow, TIMA contains a zero value for 4
                           cycles, so don't update it immediately */
                        if (t->tima == 0) {
                                t->tima_overflowed = true;
                                t->cycles_since_tima_overflow = 0;
                        }
                }
                assert(t->tima == 0 || !t->tima_overflowed);

                if (t->tima_overflowed && t->cycles_since_tima_overflow > 0) {
                        t->tima_overflowed = false;
                        t->tima = t->tma;
                        *interrupt_flag = (1U << 2);
                        t->tima_reloaded = true;
                }
                t->prev_and_result = and_result;
        }
        t->cycles_since_tima_overflow++;
}

static void write_timer(struct timer *t, u8 v, u16 addr)
{
        switch (addr) {
        case DIV_ADDR:
                t->div = 0;
                break;
        case TIMA_ADDR:;
                /* as cycles_since_tima_overflow++ in sync_timers() */
                int cycles_since_overflow = t->cycles_since_tima_overflow - 1;
                if (cycles_since_overflow == 0) {
                        t->tima_overflowed = false;
                        t->tima = v;
                } else if (cycles_since_overflow != 1) {
                        t->tima = v;
                }
                break;
        case TMA_ADDR:
                t->tma = v;
                if (t->tima_reloaded)
                        t->tima = v;
                break;
        case TAC_ADDR:
                t->tac = v;
                break;
        }
}

static u8 read_timer(struct timer *t, u16 addr)
{
        switch (addr) {
        case DIV_ADDR:  return t->div >> 8;
        case TIMA_ADDR: return t->tima;
        case TMA_ADDR:  return t->tma;
        case TAC_ADDR:  return t->tac;
        }

        assert(false);
        return 0;
}
