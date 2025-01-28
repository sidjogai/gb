static void write_p1(struct joypad *j, u8 v)
{
        j->p1 = v;
}

static u8 read_p1(struct joypad *j)
{
        
        j->p1 |= 0xC0;
        j->p1 &= 0xF0;

        /* TODO: what happens when both buttons and D-pad selected? */

        bool select_buttons = !(j->p1 & (1 << 5));
        bool select_d_pad   = !(j->p1 & (1 << 4));

        if (select_buttons)
                j->p1 |= (u8)(0x0F & ~(j->state[A]
                                       | j->state[B] << 1
                                       | j->state[SELECT] << 2
                                       | j->state[START] << 3));

        if (select_d_pad)
                j->p1 |= (u8)(0x0F & ~(j->state[RIGHT]
                                       | j->state[LEFT] << 1
                                       | j->state[UP] << 2
                                       | j->state[DOWN] << 3));

        return j->p1;
}
