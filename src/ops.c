#define to_u16(msb, lsb) ((u16)(((msb) << 8) | (lsb)))

/* ====================== Misc / control instructions ======================= */

/* halt: 1 byte, 4 cycles */
static void halt(struct cpu *cpu)
{
        cpu->is_paused = true;
        TRACE("halt");
}

/* stop: 2 bytes, 4 cycles */
static noreturn void stop(struct cpu *cpu)
{
        die("stop: not implemented\n");
}

/* di: 1 byte, 4 cycles */
static void di(struct cpu *cpu)
{
        cpu->ime.tick    = cpu->tick;
        cpu->ime.enabled = 0;

        TRACE("di");
}

/* ei: 1 byte, 4 cycles */
static void ei(struct cpu *cpu)
{
        cpu->ime.tick = cpu->tick + 4;

        TRACE("ei");
}

/* nop: 1 byte, 4 cycles */
static void nop(struct cpu *cpu)
{
        TRACE("nop");
}

/* invalid: 1 byte, 4 cycles */
/* static noreturn void invalid(struct cpu *cpu) */
static  void invalid(struct cpu *cpu)
{
        ;
}

/* ============================= Jumps / Calls ============================== */

/* jp imm16: 3 bytes, 16 cycles */
static void jp_imm16(struct cpu *cpu)
{
        u8  lsb  = cpu_fetch(cpu);
        u8  msb  = cpu_fetch(cpu);
        u16 addr = to_u16(msb, lsb);

        cpu->regs.pc   = addr;
        tick_cpu(cpu);  /* internal (branch decision?) */

        TRACE("jp %.04x", addr);
}

/* jp hl: 1 byte, 4 cycles */
static void jp_hl(struct cpu *cpu)
{
        cpu->regs.pc = cpu->regs.hl;

        TRACE("jp hl");
}

/* jp cond, imm16: 3 bytes, 16/12 cycles */
static void jp_cond_imm16(struct cpu *cpu, enum cond c)
{
        u8  lsb  = cpu_fetch(cpu);
        u8  msb  = cpu_fetch(cpu);
        u16 addr = to_u16(msb, lsb);

        if (checkcond(cpu, c)) {
                cpu->regs.pc = addr;
                tick_cpu(cpu); /* internal (branch decision?) */
        }

        TRACE("jp %s, %.04x", cond_name(c), addr);
}

/* jr, imm8: 2 bytes, 12 cycles */
static void jr_imm8(struct cpu *cpu)
{
        i8  e    = (i8)cpu_fetch(cpu);
        u16 addr = (u16)(cpu->regs.pc + e);

        cpu->regs.pc = addr;
        tick_cpu(cpu); /* internal (modify pc) */

        TRACE("jr %2d", e);
}

/* jr cond, imm8: 2 bytes, 12/8 cycles */
static void jr_cond_imm8(struct cpu *cpu, enum cond c)
{
        i8  e    = (i8)cpu_fetch(cpu);
        u16 addr = (u16)(cpu->regs.pc + e);

        if (checkcond(cpu, c)) {
                cpu->regs.pc = addr;
                tick_cpu(cpu); /* internal (modify pc) */
        }

        TRACE("jr %s, %.04x", cond_name(c), addr);
}

/* call imm16: 3 bytes, 24 cycles */
static void call_imm16(struct cpu *cpu)
{
        u8  lsb  = cpu_fetch(cpu);
        u8  msb  = cpu_fetch(cpu);
        u16 addr = to_u16(msb, lsb);

        tick_cpu(cpu); /* M2: IDU SP- */
        cpu->regs.sp--;

        cpu_write(cpu, cpu->regs.sp--, cpu->regs.pc_msb);
        cpu_write(cpu, cpu->regs.sp, cpu->regs.pc_lsb);
        cpu->regs.pc = addr;

        TRACE("call %.04x", addr);
}

/* call cond, imm16: 3 bytes, 24/12 cycles */
static void call_cond_imm16(struct cpu *cpu, enum cond c)
{
        u8  lsb  = cpu_fetch(cpu);
        u8  msb  = cpu_fetch(cpu);
        u16 addr = to_u16(msb, lsb);

        if (checkcond(cpu, c)) {
                cpu->regs.sp--;
                tick_cpu(cpu); /* M2: IDU SP- */

                cpu_write(cpu, cpu->regs.sp--, cpu->regs.pc_msb);
                cpu_write(cpu, cpu->regs.sp, cpu->regs.pc_lsb);
                cpu->regs.pc = addr;
        }

        TRACE("call %s %.04x", cond_name(c), addr);
}

/* ret: 1 byte, 16 cycles */
static void ret(struct cpu *cpu)
{
        u8  lsb  = cpu_read(cpu, cpu->regs.sp++);
        u8  msb  = cpu_read(cpu, cpu->regs.sp++);
        u16 addr = to_u16(msb, lsb);

        cpu->regs.pc = addr;
        tick_cpu(cpu); /* internal (set PC?) */

        TRACE("ret %.04x", addr);
}

/* ret cond: 1 byte, 20/8 cycles */
static void ret_cond(struct cpu *cpu, enum cond c)
{
        tick_cpu(cpu); /* internal (branch decision?) */
        if (checkcond(cpu, c)) {
                u8  lsb  = cpu_read(cpu, cpu->regs.sp++);
                u8  msb  = cpu_read(cpu, cpu->regs.sp++);
                u16 addr = to_u16(msb, lsb);
                cpu->regs.pc  = addr;
                tick_cpu(cpu); /* internal (set PC?) */
        }

        TRACE("ret %s", cond_name(c));
}

/* reti: 1 byte, 16 cycles */
static void reti(struct cpu *cpu)
{
        ret(cpu);
        ei(cpu);

        TRACE("reti");
}

/* rst tgt3: 1 byte, 16 cycles */
static void rst_tgt3(struct cpu *cpu, u8 n)
{
        tick_cpu(cpu);
        u16 addr = to_u16(0x00, n);

        cpu_write(cpu, --cpu->regs.sp, cpu->regs.pc_msb);
        cpu_write(cpu, --cpu->regs.sp, cpu->regs.pc_lsb);
        cpu->regs.pc = addr;

        TRACE("rst %.02x", n);
}

/* ======================== 8-bit load instructions ========================= */

/* ld r8, r8: 1 byte, 4 cycles */
static void ld_r8_r8(struct cpu *cpu, enum reg dst, enum reg src)
{
        u8 v = getreg8(cpu, src);

        setreg8(cpu, dst, v);

        /* if (dst == REG_D && src == REG_A) */
        /*         puts("ld D A"); */

        /* if (dst == REG_E && src == REG_A) */
        /*         puts("ld E A"); */

        /* if (dst == REG_C && src == REG_A) */
        /*         puts("ld C A"); */

        /* if (dst == REG_L && src == REG_A) */
        /*         puts("ld L A"); */

        TRACE("ld %s, %s", reg_name(dst), reg_name(src));

}

/* ld r8, imm8: 2 bytes, 8 cycles */
static void ld_r8_imm8(struct cpu *cpu, enum reg r)
{
        u8 imm8 = cpu_fetch(cpu);
        setreg8(cpu, r, imm8);

        TRACE("ld %s, %.02x", reg_name(r), imm8);
}

/* ld r8, [hl]: 1 byte, 8 cycles */
static void ld_r8_hl(struct cpu *cpu, enum reg r)
{
        u8 v = cpu_read(cpu, cpu->regs.hl);

        setreg8(cpu, r, v);

        TRACE("ld %s [hl]", reg_name(r));
}

/* ld [hl], r8: 1 byte, 8 cycles */
static void ld_hl_r8(struct cpu *cpu, enum reg r)
{
        u8 v = getreg8(cpu, r);

        cpu_write(cpu, cpu->regs.hl, v);

        TRACE("ld [hl] %s", reg_name(r));
}

/* ld [hl], imm8: 2 bytes, 12 cycles */
static void ld_hl_imm8(struct cpu *cpu)
{
        u8 imm8 = cpu_fetch(cpu);

        cpu_write(cpu, cpu->regs.hl, imm8);

        TRACE("ld [hl] %.02x", imm8);
}

/* ld a, [r16mem]: 1 byte, 8 cycles */
static void ld_a_r16mem(struct cpu *cpu, enum reg r)
{
        u16 addr = getreg16(cpu, r);
        u8  v    = cpu_read(cpu, addr);

        cpu->regs.a = v;

        TRACE("ld a, [%s]", reg_name(r));
}

/* ldi a, [hl]: 1 byte, 8 cycles */
static void ldi_a_hl(struct cpu *cpu)
{
        cpu->regs.a = cpu_read(cpu, cpu->regs.hl++);

        TRACE("ldi a, [hl]");
}

/* ldd a, [hl]: 1 byte, 8 cycles */
static void ldd_a_hl(struct cpu *cpu)
{
        cpu->regs.a = cpu_read(cpu, cpu->regs.hl--);

        TRACE("ldd a, [hl]");
}

/* ld [r16mem], a: 1 byte, 8 cycles */
static void ld_r16mem_a(struct cpu *cpu, enum reg r)
{
        u16 addr = getreg16(cpu, r);

        cpu_write(cpu, addr, cpu->regs.a);

        TRACE("ld [%s], a", reg_name(r));
}

/* ldi [hl], a: 1 byte, 8 cycles */
static void ldi_hl_a(struct cpu *cpu)
{
        cpu_write(cpu, cpu->regs.hl++, cpu->regs.a);

        TRACE("ldi [hl], a");
}

/* ldd [hl], a: 1 byte, 8 cycles */
static void ldd_hl_a(struct cpu *cpu)
{
        cpu_write(cpu, cpu->regs.hl--, cpu->regs.a);

        TRACE("ldi [hl], a");
}

/* ld a, [imm16]: 3 bytes, 16 cycles */
static void ld_a_imm16(struct cpu *cpu)
{
        u8   lsb = cpu_fetch(cpu);
        u8   msb = cpu_fetch(cpu);
        u16 addr = to_u16(msb, lsb);
        u8     v = cpu_read(cpu, addr);

        cpu->regs.a = v;

        TRACE("ld a [%.02x]", v);
}

/* ldh [imm16] a: 3 bytes, 16 cycles */
static void ldh_imm16_a(struct cpu *cpu)
{
        u8  lsb  = cpu_fetch(cpu);
        u8  msb  = cpu_fetch(cpu);
        u16 addr = to_u16(msb, lsb);

        cpu_write(cpu, addr, cpu->regs.a);

        TRACE("ldh [%.04x], a", addr);
}

/* ldh a, [c]: 1 byte, 8 cycles */
static void ldh_a_c(struct cpu *cpu)
{
        u8  lsb  = cpu->regs.c;
        u16 addr = to_u16(0xFF, lsb);
        u8  v    = cpu_read(cpu, addr);

        cpu->regs.a = v;

        TRACE("ldh a [c]");
}

/* ldh [c] a: 1 byte, 8 cycles */
static void ldh_c_a(struct cpu *cpu)
{
        u8  lsb  = cpu->regs.c;
        u16 addr = to_u16(0xFF, lsb); // NOTE FF

        cpu_write(cpu, addr, cpu->regs.a);

        TRACE("ldh [c] a");
}


/* ldh a, [imm8]: 2 bytes, 12 cycles */
static void ldh_a_imm8(struct cpu *cpu)
{

        u8  lsb  = cpu_fetch(cpu);
        u16 addr = to_u16(0xFF, lsb);
        u8  v    = cpu_read(cpu, addr);

        cpu->regs.a = v;

        TRACE("ldh a [%.04x]", addr);
}

/* ldh [imm8] a: 2 bytes, 12 cycles */
static void ldh_imm8_a(struct cpu *cpu)
{
        u8  lsb  = cpu_fetch(cpu);
        u16 addr = to_u16(0xFF, lsb);

        cpu_write(cpu, addr, cpu->regs.a);

        TRACE("ldh [%.04x] a", addr);
}

/* ======================== 16-bit load instructions ======================== */

/* ld 16, imm16: 3 bytes, 12 cycles */
static void ld_r16_imm16(struct cpu *cpu, enum reg r)
{
        u8  lsb   = cpu_fetch(cpu);
        u8  msb   = cpu_fetch(cpu);
        u16 imm16 = to_u16(msb, lsb);

        setreg16(cpu, r, imm16);

        TRACE("ld %s, $%.04x", reg_name(r), imm16);
}

/* ld [imm16], sp: 3 bytes, 20 cycles */
static void ld_imm16_sp(struct cpu *cpu)
{
        u8  lsb  = cpu_fetch(cpu);
        u8  msb  = cpu_fetch(cpu);
        u16 addr = to_u16(msb, lsb);

        cpu_write(cpu, addr++, cpu->regs.sp_lsb);
        cpu_write(cpu, addr, cpu->regs.sp_msb);

        TRACE("ld [%.04x] sp", addr);
}

/* ld sp, hl: 1 byte, 8 cycles */
static void ld_sp_hl(struct cpu *cpu)
{
        cpu->regs.sp = cpu->regs.hl;
        tick_cpu(cpu); /* internal */

        TRACE("ld sp hl");
}

/* push r16stk: 1 byte, 16 cycles */
static void push_r16stk(struct cpu *cpu, enum reg r)
{
        u16 v = getreg16(cpu, r);

        tick_cpu(cpu); /* internal */

        cpu_write(cpu, --cpu->regs.sp, (u8)(v >> 8));
        cpu_write(cpu, --cpu->regs.sp, (u8)(v & 0xFF));

        TRACE("push %s", reg_name(r));
}

/* pop r16stk: 1 byte, 12 cycles */
static void pop_r16stk(struct cpu *cpu, enum reg r)
{
        u8  lsb = cpu_read(cpu, cpu->regs.sp++);
        u8  msb = cpu_read(cpu, cpu->regs.sp++);

        /* the bottom nibble of the flags register is always zero */
        if (r == REG_AF)
                lsb &= 0xF0;

        u16 v = to_u16(msb, lsb);

        setreg16(cpu, r, v);

        TRACE("pop %s", reg_name(r));
}

/* ld hl, sp + imm8: 2 bytes, 12 cycles, flags: Z=0, N=0, H, C */
static void ld_hl_sp_imm8(struct cpu *cpu)
{
        i8  e  = (i8)cpu_fetch(cpu);
        u16 sp = cpu->regs.sp;
        u16 v  = (u16)(sp + e);

        tick_cpu(cpu); /* internal */
        cpu->regs.hl = v;

        setflag_z(cpu, 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, (e & 0xF) + (sp & 0xF) > 0xF);
        setflag_c(cpu, (u8)e + (sp & 0xFF) > 0xFF);

        TRACE("ld hl, sp + %.02d", e);
}

/* ================ 8-bit arithmetic / logical instructions ================= */

/* add a, r8: 1 byte, 4 cycles, flags: Z, N=0, H, C */
static void add_a_r8(struct cpu *cpu, enum reg r)
{
        u8 a  = cpu->regs.a;
        u8 rv = getreg8(cpu, r);
        u8 v  = a + rv;

        cpu->regs.a = v;

        setflag_z(cpu, v == 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, (a & 0xF) + (rv & 0xF) > 0xF);
        setflag_c(cpu, a + rv > 0xFF);

        TRACE("add a %s", reg_name(r));
}

/* add a, [hl]: 1 byte, 8 cycles, flags: Z, N=0, H, C */
static void add_a_hl(struct cpu *cpu)
{
        u8 a  = cpu->regs.a;
        u8 rv = cpu_read(cpu, cpu->regs.hl);
        u8 v  = a + rv;

        cpu->regs.a = v;

        setflag_z(cpu, v == 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, (a & 0xF) + (rv & 0xF) > 0xF);
        setflag_c(cpu, a + rv > 0xFF);

        TRACE("add a hl");
}

/* add a, imm8: 2 bytes, 8 cycles, flags: Z, N=0, H, C */
static void add_a_imm8(struct cpu *cpu)
{
        u8 old = cpu_fetch(cpu);
        u8 a   = cpu->regs.a;
        u8 new = a + old;

        cpu->regs.a = new;

        setflag_z(cpu, new == 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, (old & 0xF) + (a & 0xF) > 0xF);
        setflag_c(cpu, old + a > 0xFF);

        TRACE("add a, %.02x", old);
}

/* adc a, r8: 1 byte, 4 cycles, flags: Z, N=0, H, C */
static void adc_a_r8(struct cpu *cpu, enum reg r)
{
        u8 rv = getreg8(cpu, r);
        u8 a  = cpu->regs.a;
        u8 c  = isflagset_c(cpu);
        u8 v  = (u8)(rv + a + c);

        cpu->regs.a = v;

        setflag_z(cpu, v == 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, (rv & 0xF) + (a & 0xF) + c > 0xF);
        setflag_c(cpu, v < rv + a + c);

        TRACE("adc a %s", reg_name(r));
}

/* adc a, [hl]: 1 byte, 8 cycles, flags: Z, N=0, H, C */
static void adc_a_hl(struct cpu *cpu)
{
        u8 rv = cpu_read(cpu, cpu->regs.hl);
        u8 a  = cpu->regs.a;
        u8 c  = isflagset_c(cpu);
        u8 v  = (u8)(rv + a + c);

        cpu->regs.a = v;

        setflag_z(cpu, v == 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, (rv & 0xF) + (a & 0xF) + c > 0xF);
        setflag_c(cpu, v < rv + a + c);

        TRACE("adc a hl");
}

/* adc a, imm8: 2 bytes, 8 cycles, flags: Z, N=0, H, C */
static void adc_a_imm8(struct cpu *cpu)
{
        u8 n = cpu_fetch(cpu);
        u8 a = cpu->regs.a;
        u8 c = isflagset_c(cpu);
        u8 v = (u8)(n + a + c);

        cpu->regs.a = v;

        setflag_z(cpu, v == 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, (n & 0xF) + (a & 0xF) + c > 0xF);
        setflag_c(cpu, v < n + a + c);

        TRACE("adc a %.02x", n);
}

/* sub a, r8: 1 byte, 4 cycles, flags: Z, N=1, H, C */
static void sub_a_r8(struct cpu *cpu, enum reg r)
{
        u8 rv = getreg8(cpu, r);
        u8 a  = cpu->regs.a;
        u8 v  = a - rv;

        cpu->regs.a = v;

        setflag_z(cpu, v == 0);
        setflag_n(cpu, 1);
        setflag_h(cpu, (rv & 0xF) > (a & 0xF));
        setflag_c(cpu, rv > a);

        TRACE("sub a %s", reg_name(r));
}

/* sub a, [hl]: 1 byte, 8 cycles, flags: Z, N=1, H, C */
static void sub_a_hl(struct cpu *cpu)
{
        u8 rv = cpu_read(cpu, cpu->regs.hl);
        u8 a  = cpu->regs.a;
        u8 v  = a - rv;

        cpu->regs.a = v;

        setflag_z(cpu, v == 0);
        setflag_n(cpu, 1);
        setflag_h(cpu, (rv & 0xF) > (a & 0xF));
        setflag_c(cpu, rv > a);

        TRACE("sub_a_hl");
}

/* sub a, imm8: 2 bytes, 8 cycles, flags: Z, N=1, H, C */
static void sub_a_imm8(struct cpu *cpu)
{
        u8 old = cpu_fetch(cpu);
        u8 a   = cpu->regs.a;
        u8 new = a - old;

        cpu->regs.a = new;

        setflag_z(cpu, new == 0);
        setflag_n(cpu, 1);
        setflag_h(cpu, (old & 0xF) > (a & 0xF));
        setflag_c(cpu, old > a);

        TRACE("sub a %.02x", old);
}

/* sbc a, r8: 1 byte, 4 cycles, flags: Z, N=1, H, C */
static void sbc_a_r8(struct cpu *cpu, enum reg r)
{
        u8 rv = getreg8(cpu, r);
        u8 a  = cpu->regs.a;
        u8 c  = isflagset_c(cpu);
        u8 v  = (u8)(a - rv - c);

        cpu->regs.a = v;

        setflag_z(cpu, v == 0);
        setflag_n(cpu, 1);
        setflag_h(cpu, (a & 0xF) - (rv & 0xF) - c < 0);
        setflag_c(cpu, rv + c > a);

        TRACE("sbc a %s", reg_name(r));
}

/* sbc a, [hl]: 1 byte, 8 cycles, flags: Z, N=1, H, C */
static void sbc_a_hl(struct cpu *cpu)
{
        u8 rv = cpu_read(cpu, cpu->regs.hl);
        u8 a  = cpu->regs.a;
        u8 c  = isflagset_c(cpu);
        u8 v  = (u8)(a - rv - c);

        cpu->regs.a = v;

        setflag_z(cpu, v == 0);
        setflag_n(cpu, 1);
        setflag_h(cpu, (a & 0xF) - (rv & 0xF) - c < 0);
        setflag_c(cpu, rv + c > a);

        TRACE("sbc a hl");
}

/* sbc a, imm8: 2 bytes, 8 cycles, flags: Z, N=1, H, C */
static void sbc_a_imm8(struct cpu *cpu)
{
        u8 old = cpu_fetch(cpu);
        u8 a   = cpu->regs.a;
        u8 c   = isflagset_c(cpu);
        u8 new = (u8)(a - old - c);

        cpu->regs.a = new;

        setflag_z(cpu, new == 0);
        setflag_n(cpu, 1);
        setflag_h(cpu, (a & 0xF) - (old & 0xF) - c < 0);
        setflag_c(cpu, old + c > a);

        TRACE("sbc a %.02x", old);
}

/* cp a, r8: 1 byte, 4 cycles, flags: Z, N=1, H, C */
static void cp_a_r8(struct cpu *cpu, enum reg r)
{
        u8 a  = cpu->regs.a;
        u8 rv = getreg8(cpu, r);
        u8 v  = a - rv;

        setflag_z(cpu, v == 0);
        setflag_n(cpu, 1);
        setflag_h(cpu, (a & 0xF) - (rv &0xF) < 0);
        setflag_c(cpu, a - rv < 0);

        TRACE("cp %s", reg_name(r));
}

/* cp a, [hl]: 1 byte, 8 cycles, flags: Z, N=1, H, C */
static void cp_a_hl(struct cpu *cpu)
{
        u8  a    = cpu->regs.a;
        u16 addr = cpu->regs.hl;
        u8  n    = cpu_read(cpu, addr);
        u8  v    = a - n;

        setflag_z(cpu, v == 0);
        setflag_n(cpu, 1);
        setflag_h(cpu, (a & 0xF) - (n &0xF) < 0);
        setflag_c(cpu, a - n < 0);

        TRACE("cp hl");
}

/* cp a, imm8: 2 bytes, 8 cycles, flags: Z, N=1, H, C */
static void cp_a_imm8(struct cpu *cpu)
{
        u8 old = cpu_fetch(cpu);
        u8 a   = cpu->regs.a;
        u8 new = a - old;

        setflag_z(cpu, new == 0);
        setflag_n(cpu, 1);
        setflag_h(cpu, (old & 0xF) > (a & 0xF));
        setflag_c(cpu, old > a);

        TRACE("cp $%02X", old);
}

/* inc r8: 1 byte, 4 cycles, flags: Z, N=0, H */
static void inc_r8(struct cpu *cpu, enum reg r)
{
        u8 old = getreg8(cpu, r);
        u8 new = old + 1;

        setreg8(cpu, r, new);

        setflag_z(cpu, new == 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, (old & 0xF) == 0xF);

        TRACE("inc %s", reg_name(r));
}

/* inc [hl]: 1 byte, 12 cycles, flags: Z, N=0, H */
static void inc_hl(struct cpu *cpu)
{
        u8 old = cpu_read(cpu, cpu->regs.hl);
        u8 new = old + 1;

        cpu_write(cpu, cpu->regs.hl, new);

        setflag_z(cpu, new == 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, (old & 0xF) == 0xF);

        TRACE("inc hl");
}

/* dec r8: 1 byte, 4 cycles, flags: Z, N=1, H */
static void dec_r8(struct cpu *cpu, enum reg r)
{
        u8 old = getreg8(cpu, r);
        u8 new = old - 1;

        setreg8(cpu, r, new);

        setflag_z(cpu, new == 0);
        setflag_n(cpu, 1);
        setflag_h(cpu, (new & 0xF) == 0xF);

        TRACE("dec %s", reg_name(r));
}

/* dec [hl]: 1 byte, 12 cycles, flags: Z, N=1, H */
static void dec_hl(struct cpu *cpu)
{
        u16 addr = cpu->regs.hl;
        u8  old  = cpu_read(cpu, addr);
        u8  new  = old - 1;

        cpu_write(cpu, addr, new);

        setflag_z(cpu, new == 0);
        setflag_n(cpu, 1);
        setflag_h(cpu, (new & 0xF) == 0xF);

        TRACE("dec hl");
}

/* and a, r8: 1 byte, 4 cycles, flags: Z, N=0, H=1, C=0 */
static void and_a_r8(struct cpu *cpu, enum reg r)
{
        u8 v = getreg8(cpu, r);

        v &= cpu->regs.a;
        cpu->regs.a = v;

        setflag_z(cpu, v == 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, 1);
        setflag_c(cpu, 0);

        TRACE("and a %s", reg_name(r));
}

/* and a, [hl]: 1 byte, 8 cycles, flags: Z, N=0, H=1, C=0 */
static void and_a_hl(struct cpu *cpu)
{
        u8 v = cpu_read(cpu, cpu->regs.hl);

        v &= cpu->regs.a;
        cpu->regs.a = v;

        setflag_z(cpu, v == 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, 1);
        setflag_c(cpu, 0);

        TRACE("and a hl");
}

/* and a, imm8: 2 bytes, 8 cycles, flags: Z, N=0, H=1, C=0 */
static void and_a_imm8(struct cpu *cpu)
{
        u8 v = cpu_fetch(cpu);

        v &= cpu->regs.a;
        cpu->regs.a = v;

        setflag_z(cpu, v == 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, 1);
        setflag_c(cpu, 0);

        TRACE("and a, $%.02x", v);
}

/* or r8: 1 byte, 4 cycles, flags: Z, N=0, H=0, C=0 */
static void or_r8(struct cpu *cpu, enum reg r)
{
        u8 v = getreg8(cpu, r);

        v |= cpu->regs.a;
        cpu->regs.a = v;

        setflag_z(cpu, v == 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, 0);
        setflag_c(cpu, 0);

        TRACE("or %s", reg_name(r));
}

/* or [hl]: 1 byte, 8 cycles, flags: Z, N=0, H=0, C=0 */
static void or_hl(struct cpu *cpu)
{
        u16 addr = cpu->regs.hl;
        u8     v = cpu_read(cpu, addr);

        v |= cpu->regs.a;
        cpu->regs.a = v;

        setflag_z(cpu, v == 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, 0);
        setflag_c(cpu, 0);

        TRACE("or [hl]");
}

/* or imm8: 2 bytes, 8 cycles, flags: Z, N=0, H=0, C=0 */
static void or_imm8(struct cpu *cpu)
{
        u8 imm8 = cpu_fetch(cpu);
        u8 v    = imm8 | cpu->regs.a;
        cpu->regs.a = v;

        setflag_z(cpu, v == 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, 0);
        setflag_c(cpu, 0);

        TRACE("or %.02x", imm8);
}

/* xor a, r8: 1 byte, 4 cycles, flags: Z, N=0, H=0, C=0 */
static void xor_a_r8(struct cpu *cpu, enum reg r)
{
        u8 v = getreg8(cpu, r);

        v ^= cpu->regs.a;
        cpu->regs.a = v;

        setflag_z(cpu, v == 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, 0);
        setflag_c(cpu, 0);

        TRACE("xor %s", reg_name(r));
}

/* xor a, [hl]: 1 byte, 8 cycles, flags: Z, N=0, H=0, C=0 */
static void xor_a_hl(struct cpu *cpu)
{
        u16 addr = cpu->regs.hl;
        u8     v = cpu_read(cpu, addr);

        v ^= cpu->regs.a;
        cpu->regs.a = v;

        setflag_z(cpu, v == 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, 0);
        setflag_c(cpu, 0);

        TRACE("xor a hl");
}

/* xor a, imm8: 2 bytes, 8 cycles, flags: Z, N=0, H=0, C=0 */
static void xor_a_imm8(struct cpu *cpu)
{
        u8 v = cpu_fetch(cpu);

        v ^= cpu->regs.a;
        cpu->regs.a = v;

        setflag_z(cpu, v == 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, 0);
        setflag_c(cpu, 0);

        TRACE("xor a %.02x", v);
}

/* ccf: 1 byte, 4 cycles, flags: N=0, H=0, C */
static void ccf(struct cpu *cpu)
{
        u8 v = !isflagset_c(cpu);

        setflag_n(cpu, 0);
        setflag_h(cpu, 0);
        setflag_c(cpu, v);

        TRACE("ccf");
}

/* scf: 1 byte, 4 cycles, flags: N=0, H=0, C=1 */
static void scf(struct cpu *cpu)
{
        setflag_n(cpu, 0);
        setflag_h(cpu, 0);
        setflag_c(cpu, 1);

        TRACE("scf");
}

/* daa: 1 byte, 4 cycles, flags: Z, H=0, C */
static void daa(struct cpu *cpu)
{
        /* https://forums.nesdev.org/viewtopic.php?p=196282 */

        u8 a = cpu->regs.a;
        u8 n = isflagset_n(cpu);
        u8 h = isflagset_h(cpu);
        u8 c = isflagset_c(cpu);

        if (n) {
                if (c)
                        a -= 0x60;
                if (h)
                        a -= 0x6;
        } else {
                if (c || a > 0x99) {
                        a += 0x60;
                        c = true;
                }
                if (h || (a & 0x0f) > 0x09)
                        a += 0x6;
        }

        cpu->regs.a = a;

        setflag_z(cpu, a == 0);
        setflag_h(cpu, 0);
        setflag_c(cpu, c);

        TRACE("daa");
}

/* cpl: 1 byte, 4 cycles, flags: N=1, H=1 */
static void cpl(struct cpu *cpu)
{
        cpu->regs.a = ~cpu->regs.a;

        setflag_n(cpu, 1);
        setflag_h(cpu, 1);

        TRACE("cpl");
}

/* ================ 16-bit arithmetic / logical instructions ================ */

/* inc r16: 1 byte, 8 cycles */
static void inc_r16(struct cpu *cpu, enum reg r)
{
        u16 v = getreg16(cpu, r);

        setreg16(cpu, r, ++v);
        tick_cpu(cpu); /* internal */

        TRACE("inc %s", reg_name(r));
}

/* dec r16: 1 byte, 8 cycles */
static void dec_r16(struct cpu *cpu, enum reg r)
{
        u16 v = getreg16(cpu, r);

        setreg16(cpu, r, --v);
        tick_cpu(cpu); /* internal */

        TRACE("dec %s", reg_name(r));
}

/* add hl, r16: 1 byte, 8 cycles, flags: N=0, H, C */
static void add_hl_r16(struct cpu *cpu, enum reg r)
{
        u16 hl = cpu->regs.hl;
        u16 rv = getreg16(cpu, r);
        u16 v  = hl + rv;

        cpu->regs.hl = v;

        setflag_n(cpu, 0);
        setflag_h(cpu, ((hl & 0xFFF) + (rv & 0xFFF)) > 0xFFF);
        setflag_c(cpu, hl + rv > 0xFFFF);

        tick_cpu(cpu); /* internal */

        TRACE("add hl %s", reg_name(r));
}

/* add sp, imm8: 2 bytes, 16 cycles, flags: Z=0, N=0, H, C */
static void add_sp_imm8(struct cpu *cpu)
{
        i8   e  = (i8)cpu_fetch(cpu);
        u16 sp = cpu->regs.sp;
        u16 v  = (u16)(sp + e);

        tick_cpu(cpu); /* internal */
        cpu->regs.sp = v;
        tick_cpu(cpu); /* write */

        setflag_z(cpu, 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, (e & 0xF) + (sp & 0xF) > 0xF);
        setflag_c(cpu, (u8)e + (sp & 0xFF) > 0xFF);

        TRACE("add sp %.02d", e);
}

/* =============== 8-bit shift, rotate, and bit instructions ================ */

/* rlca: 1 byte, 4 cycles, flags: Z=0, N=0, H=0, C */
static void rlca(struct cpu *cpu)
{
        u8 a    = cpu->regs.a;
        u8 bit7 = (a >> 7) & 1;
        u8 v    = (u8)(a << 1) | bit7;

        cpu->regs.a = v;

        setflag_z(cpu, 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, 0);
        setflag_c(cpu, bit7);

        TRACE("rcla");
}

/* rrca: 1 byte, 4 cycles, flags: Z=0, N=0, H=0, C */
static void rrca(struct cpu *cpu)
{
        u8 a    = cpu->regs.a;
        u8 bit0 = a & 1;
        u8 v    = (u8)(a >> 1) | (u8)(bit0 << 7);

        cpu->regs.a = v;

        setflag_z(cpu, 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, 0);
        setflag_c(cpu, bit0);

        TRACE("rrca");
}

/* rla: 1 byte, 4 cycles, flags: Z=0, N=0, H=0, C */
static void rla(struct cpu *cpu)
{
        u8 a    = cpu->regs.a;
        u8 bit7 = (a >> 7) & 1;
        u8 c    = isflagset_c(cpu);
        u8 v    = (u8)(a << 1) | c;

        cpu->regs.a = v;

        setflag_z(cpu, 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, 0);
        setflag_c(cpu, bit7);

        TRACE("rla");
}

/* rra: 1 byte, 4 cycles, flags: Z=0, N=0, H=0, C */
static void rra(struct cpu *cpu)
{
        u8 a    = cpu->regs.a;
        u8 c    = isflagset_c(cpu);
        u8 bit0 = a & 01;
        u8 v    = (u8)(c << 7) | (a >> 1);

        cpu->regs.a = v;

        setflag_z(cpu, 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, 0);
        setflag_c(cpu, bit0);

        TRACE("rra");
}

/* rlc r8: 2 bytes, 8 cycles, flags: Z, N=0, H=0, C */
static void rlc_r8(struct cpu *cpu, enum reg r)
{
        u8 rv   = getreg8(cpu, r);
        u8 bit7 = (rv >> 7) & 1;
        u8 v    = (u8)(rv << 1) | bit7;

        setreg8(cpu, r, v);

        setflag_z(cpu, v == 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, 0);
        setflag_c(cpu, bit7);

        TRACE("rlc %s", reg_name(r));
}

/* rlc [hl]: 2 bytes, 16 cycles, flags: Z, N=0, H=0, C */
static void rlc_hl(struct cpu *cpu)
{
        u8 rv   = cpu_read(cpu, cpu->regs.hl);
        u8 bit7 = (rv >> 7) & 1;
        u8 v    = (u8)(rv << 1) | bit7;

        cpu_write(cpu, cpu->regs.hl, v);

        setflag_z(cpu, v == 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, 0);
        setflag_c(cpu, bit7);

        TRACE("rlc hl");
}

/* rrc r8: 2 bytes, 8 cycles, flags: Z, N=0, H=0, C */
static void rrc_r8(struct cpu *cpu, enum reg r)
{
        u8 rv   = getreg8(cpu, r);
        u8 bit0 = rv & 1;
        u8 v    = (u8)(rv >> 1) | (u8)(bit0 << 7);

        setreg8(cpu, r, v);

        setflag_z(cpu, v == 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, 0);
        setflag_c(cpu, bit0);

        TRACE("rrc %s", reg_name(r));
}

/* rrc [hl]: 2 bytes, 16 cycles, flags: Z, N=0, H=0, C */
static void rrc_hl(struct cpu *cpu)
{
        u8 rv   = cpu_read(cpu, cpu->regs.hl);
        u8 bit0 = rv & 1;
        u8 v    = (u8)(rv >> 1) | (u8)(bit0 << 7);

        cpu_write(cpu, cpu->regs.hl, v);

        setflag_z(cpu, v == 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, 0);
        setflag_c(cpu, bit0);

        TRACE("rrc hl");
}

/* rl r8: 2 bytes, 8 cycles, flags: Z, N=0, H=0, C */
static void rl_r8(struct cpu *cpu, enum reg r)
{
        u8 rv   = getreg8(cpu, r);
        u8 bit7 = (rv >>7) & 1;
        u8 c    = isflagset_c(cpu);
        u8 v    = (u8)(rv << 1) | c;

        setreg8(cpu, r, v);

        setflag_z(cpu, v == 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, 0);
        setflag_c(cpu, bit7);

        TRACE("rl %s", reg_name(r));
}

/* rl [hl]: 2 bytes, 16 cycles, flags: Z, N=0, H=0, C */
static void rl_hl(struct cpu *cpu)
{
        u8 rv   = cpu_read(cpu, cpu->regs.hl);
        u8 bit7 = (rv >>7) & 1;
        u8 c    = isflagset_c(cpu);
        u8 v    = (u8)(rv << 1) | c;

        cpu_write(cpu, cpu->regs.hl, v);

        setflag_z(cpu, v == 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, 0);
        setflag_c(cpu, bit7);

        TRACE("rl hl");
}

/* rr r8: 2 bytes, 8 cycles, flags: Z, N=0, H=0, C */
static void rr_r8(struct cpu *cpu, enum reg r)
{
        u8 rv   = getreg8(cpu, r);
        u8 bit0 = rv & 1;
        u8 c    = isflagset_c(cpu);
        u8 v    = (u8)(c << 7) | rv >> 1;

        setreg8(cpu, r, v);

        setflag_z(cpu, v == 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, 0);
        setflag_c(cpu, bit0);

        TRACE("r %s", reg_name(r));
}

/* rr [hl]: 2 bytes, 16 cycles, flags: Z, N=0, H=0, C */
static void rr_hl(struct cpu *cpu)
{
        u8 rv   = cpu_read(cpu, cpu->regs.hl);
        u8 bit0 = rv & 1;
        u8 c    = isflagset_c(cpu);
        u8 v    = (u8)(c << 7) | rv >> 1;

        cpu_write(cpu, cpu->regs.hl, v);

        setflag_z(cpu, v == 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, 0);
        setflag_c(cpu, bit0);

        TRACE("rr hl");
}

/* sla r8: 2 bytes, 8 cycles, flags: Z, N=0, H=0, C */
static void sla_r8(struct cpu *cpu, enum reg r)
{
        u8 rv   = getreg8(cpu, r);
        u8 bit7 = (rv >> 7) & 1;
        u8 v    = (u8)(rv << 1);

        setreg8(cpu, r, v);

        setflag_z(cpu, v == 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, 0);
        setflag_c(cpu, bit7);

        TRACE("sla %s", reg_name(r));
}

/* sla [hl]: 2 bytes, 16 cycles, flags: Z, N=0, H=0, C */
static void sla_hl(struct cpu *cpu)
{
        u8 rv   = cpu_read(cpu, cpu->regs.hl);
        u8 bit7 = (rv >> 7) & 1;
        u8 v    = (u8)(rv << 1);

        cpu_write(cpu, cpu->regs.hl, v);

        setflag_z(cpu, v == 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, 0);
        setflag_c(cpu, bit7);

        TRACE("sla [hl]");
}

/* sra r8: 2 bytes, 8 cycles, flags: Z, N=0, H=0, C */
static void sra_r8(struct cpu *cpu, enum reg r)
{
        u8 rv   = getreg8(cpu, r);
        u8 mask = rv & 0x80;
        u8 bit0 = rv & 1;
        u8 v    = (u8)(rv >> 1) | mask;

        setreg8(cpu, r, v);

        setflag_z(cpu, v == 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, 0);
        setflag_c(cpu, bit0);

        TRACE("sra %s", reg_name(r));
}

/* sra [hl]: 2 bytes, 16 cycles, flags: Z, N=0, H=0, C */
static void sra_hl(struct cpu *cpu)
{
        u8 rv   = cpu_read(cpu, cpu->regs.hl);
        u8 mask = rv & 0x80;
        u8 bit0 = rv & 1;
        u8 v    = (u8)(rv >> 1) | mask;

        cpu_write(cpu, cpu->regs.hl, v);

        setflag_z(cpu, v == 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, 0);
        setflag_c(cpu, bit0);

        TRACE("sra [hl]");
}

/* swap r8: 2 bytes, 8 cycles, flags: Z, N=0, H=0, C=0 */
static void swap_r8(struct cpu *cpu, enum reg r)
{
        u8 rv = getreg8(cpu, r);
        u8 lo = rv & 0x0F;
        u8 hi = rv & 0xF0;
        u8 v  = (u8)(lo << 4) | (u8)(hi >> 4);

        setreg8(cpu, r, v);

        setflag_z(cpu, v == 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, 0);
        setflag_c(cpu, 0);

        TRACE("swap %s", reg_name(r));
}

/* swap [hl]: 2 bytes, 16 cycles, flags: Z, N=0, H=0, C=0 */
static void swap_hl(struct cpu *cpu)
{
        u8 rv = cpu_read(cpu, cpu->regs.hl);
        u8 lo = rv & 0x0F;
        u8 hi = rv & 0xF0;
        u8 v  = (u8)(lo << 4) | (u8)(hi >> 4);

        cpu_write(cpu, cpu->regs.hl, v);

        setflag_z(cpu, v == 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, 0);
        setflag_c(cpu, 0);

        TRACE("swap [hl]");
}

/* srl r8: 2 bytes, 8 cycles, flags: Z, N=0, H=0, C */
static void srl_r8(struct cpu *cpu, enum reg r)
{
        u8 old = getreg8(cpu, r);
        u8 new = old >> 1;

        setreg8(cpu, r, new);

        setflag_z(cpu, new == 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, 0);
        setflag_c(cpu, old & 0x1);

        TRACE("srl %s", reg_name(r));
}

/* srl [hl]: 2 bytes, 16 cycles, flags: Z, N=0, H=0, C */
static void srl_hl(struct cpu *cpu)
{
        u8 old = cpu_read(cpu, cpu->regs.hl);
        u8 new = old >> 1;

        cpu_write(cpu, cpu->regs.hl, new);

        setflag_z(cpu, new == 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, 0);
        setflag_c(cpu, old & 0x1);

        TRACE("srl [hl]");
}

/* bit b3, r8: 2 bytes, 8 cycles, flags: Z, N=0, H=1 */
static void bit_b3_r8(struct cpu *cpu, u8 bit, enum reg r)
{
        u8 rv = getreg8(cpu, r);
        u8 v  = (rv >> bit) & 1;

        setflag_z(cpu, v == 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, 1);

        TRACE("bit %x %s", bit, reg_name(r));
}

/* bit b3, [hl]: 2 bytes, 12 cycles, flags: Z, N=0, H=1 */
static void bit_b3_hl(struct cpu *cpu, u8 bit)
{
        u8 rv = cpu_read(cpu, cpu->regs.hl);
        u8 v  = (rv >> bit) & 1;

        setflag_z(cpu, v == 0);
        setflag_n(cpu, 0);
        setflag_h(cpu, 1);

        TRACE("bit %x, [hl]", bit);
}

/* res b3, r8: 2 bytes, 8 cycles */
static void res_b3_r8(struct cpu *cpu, u8 bit, enum reg r)
{
        u8 rv   = getreg8(cpu, r);
        u8 mask = (u8)~(1U << bit);
        u8 v    = rv & mask;

        setreg8(cpu, r, v);

        TRACE("res %x %s", bit, reg_name(r));
}

/* res b3, [hl]: 2 bytes, 16 cycles */
static void res_b3_hl(struct cpu *cpu, u8 bit)
{
        u8 rv   = cpu_read(cpu, cpu->regs.hl);
        u8 mask = (u8)~(1U << bit);
        u8 v    = rv & mask;

        cpu_write(cpu, cpu->regs.hl, v);

        TRACE("res %x hl", v);
}

/* set b3, r8: 2 bytes, 8 cycles */
static void set_b3_r8(struct cpu *cpu, u8 bit, enum reg r)
{
        u8 rv = getreg8(cpu, r);
        u8 v  = rv | (u8)(1U << bit);

        setreg8(cpu, r, v);

        TRACE("set %x %s", bit, reg_name(r));
}

/* set b3, [hl]: 2 bytes, 16 cycles */
static void set_b3_hl(struct cpu *cpu, u8 bit)
{
        u8 rv = cpu_read(cpu, cpu->regs.hl);
        u8 v  = rv | (u8)(1U << bit);

        cpu_write(cpu, cpu->regs.hl, v);

        TRACE("set %x hl", bit);
}

static void dispatch_prefixed_op(struct cpu *cpu);

static void dispatch_op(struct cpu *cpu, u8 op) {
        switch (op) {
        case 0x00: nop(cpu);                      break;
        case 0x01: ld_r16_imm16(cpu, REG_BC);     break;
        case 0x02: ld_r16mem_a(cpu, REG_BC);      break;
        case 0x03: inc_r16(cpu, REG_BC);          break;
        case 0x04: inc_r8(cpu, REG_B);            break;
        case 0x05: dec_r8(cpu, REG_B);            break;
        case 0x06: ld_r8_imm8(cpu, REG_B);        break;
        case 0x07: rlca(cpu);                     break;
        case 0x08: ld_imm16_sp(cpu);              break;
        case 0x09: add_hl_r16(cpu, REG_BC);       break;
        case 0x0A: ld_a_r16mem(cpu, REG_BC);      break;
        case 0x0B: dec_r16(cpu, REG_BC);          break;
        case 0x0C: inc_r8(cpu, REG_C);            break;
        case 0x0D: dec_r8(cpu, REG_C);            break;
        case 0x0E: ld_r8_imm8(cpu, REG_C);        break;
        case 0x0F: rrca(cpu);                     break;
        case 0x10: stop(cpu);                     break;
        case 0x11: ld_r16_imm16(cpu, REG_DE);     break;
        case 0x12: ld_r16mem_a(cpu, REG_DE);      break;
        case 0x13: inc_r16(cpu, REG_DE);          break;
        case 0x14: inc_r8(cpu, REG_D);            break;
        case 0x15: dec_r8(cpu, REG_D);            break;
        case 0x16: ld_r8_imm8(cpu, REG_D);        break;
        case 0x17: rla(cpu);                      break;
        case 0x18: jr_imm8(cpu);                  break;
        case 0x19: add_hl_r16(cpu, REG_DE);       break;
        case 0x1A: ld_a_r16mem(cpu, REG_DE);      break;
        case 0x1B: dec_r16(cpu, REG_DE);          break;
        case 0x1C: inc_r8(cpu, REG_E);            break;
        case 0x1D: dec_r8(cpu, REG_E);            break;
        case 0x1E: ld_r8_imm8(cpu, REG_E);        break;
        case 0x1F: rra(cpu);                      break;
        case 0x20: jr_cond_imm8(cpu, COND_NZ);    break;
        case 0x21: ld_r16_imm16(cpu, REG_HL);     break;
        case 0x22: ldi_hl_a(cpu);                 break;
        case 0x23: inc_r16(cpu, REG_HL);          break;
        case 0x24: inc_r8(cpu, REG_H);            break;
        case 0x25: dec_r8(cpu, REG_H);            break;
        case 0x26: ld_r8_imm8(cpu, REG_H);        break;
        case 0x27: daa(cpu);                      break;
        case 0x28: jr_cond_imm8(cpu, COND_Z);     break;
        case 0x29: add_hl_r16(cpu, REG_HL);       break;
        case 0x2A: ldi_a_hl(cpu);                 break;
        case 0x2B: dec_r16(cpu, REG_HL);          break;
        case 0x2C: inc_r8(cpu, REG_L);            break;
        case 0x2D: dec_r8(cpu, REG_L);            break;
        case 0x2E: ld_r8_imm8(cpu, REG_L);        break;
        case 0x2F: cpl(cpu);                      break;
        case 0x30: jr_cond_imm8(cpu, COND_NC);    break;
        case 0x31: ld_r16_imm16(cpu, REG_SP);     break;
        case 0x32: ldd_hl_a(cpu);                 break;
        case 0x33: inc_r16(cpu, REG_SP);          break;
        case 0x34: inc_hl(cpu);                   break;
        case 0x35: dec_hl(cpu);                   break;
        case 0x36: ld_hl_imm8(cpu);               break;
        case 0x37: scf(cpu);                      break;
        case 0x38: jr_cond_imm8(cpu, COND_C);     break;
        case 0x39: add_hl_r16(cpu, REG_SP);       break;
        case 0x3A: ldd_a_hl(cpu);                 break;
        case 0x3B: dec_r16(cpu, REG_SP);          break;
        case 0x3C: inc_r8(cpu, REG_A);            break;
        case 0x3D: dec_r8(cpu, REG_A);            break;
        case 0x3E: ld_r8_imm8(cpu, REG_A);        break;
        case 0x3F: ccf(cpu);                      break;
        case 0x40: ld_r8_r8(cpu, REG_B, REG_B);   break;
        case 0x41: ld_r8_r8(cpu, REG_B, REG_C);   break;
        case 0x42: ld_r8_r8(cpu, REG_B, REG_D);   break;
        case 0x43: ld_r8_r8(cpu, REG_B, REG_E);   break;
        case 0x44: ld_r8_r8(cpu, REG_B, REG_H);   break;
        case 0x45: ld_r8_r8(cpu, REG_B, REG_L);   break;
        case 0x46: ld_r8_hl(cpu, REG_B);          break;
        case 0x47: ld_r8_r8(cpu, REG_B, REG_A);   break;
        case 0x48: ld_r8_r8(cpu, REG_C, REG_B);   break;
        case 0x49: ld_r8_r8(cpu, REG_C, REG_C);   break;
        case 0x4A: ld_r8_r8(cpu, REG_C, REG_D);   break;
        case 0x4B: ld_r8_r8(cpu, REG_C, REG_E);   break;
        case 0x4C: ld_r8_r8(cpu, REG_C, REG_H);   break;
        case 0x4D: ld_r8_r8(cpu, REG_C, REG_L);   break;
        case 0x4E: ld_r8_hl(cpu, REG_C);          break;
        case 0x4F: ld_r8_r8(cpu, REG_C, REG_A);   break;
        case 0x50: ld_r8_r8(cpu, REG_D, REG_B);   break;
        case 0x51: ld_r8_r8(cpu, REG_D, REG_C);   break;
        case 0x52: ld_r8_r8(cpu, REG_D, REG_D);   break;
        case 0x53: ld_r8_r8(cpu, REG_D, REG_E);   break;
        case 0x54: ld_r8_r8(cpu, REG_D, REG_H);   break;
        case 0x55: ld_r8_r8(cpu, REG_D, REG_L);   break;
        case 0x56: ld_r8_hl(cpu, REG_D);          break;
        case 0x57: ld_r8_r8(cpu, REG_D, REG_A);   break;
        case 0x58: ld_r8_r8(cpu, REG_E, REG_B);   break;
        case 0x59: ld_r8_r8(cpu, REG_E, REG_C);   break;
        case 0x5A: ld_r8_r8(cpu, REG_E, REG_D);   break;
        case 0x5B: ld_r8_r8(cpu, REG_E, REG_E);   break;
        case 0x5C: ld_r8_r8(cpu, REG_E, REG_H);   break;
        case 0x5D: ld_r8_r8(cpu, REG_E, REG_L);   break;
        case 0x5E: ld_r8_hl(cpu, REG_E);          break;
        case 0x5F: ld_r8_r8(cpu, REG_E, REG_A);   break;
        case 0x60: ld_r8_r8(cpu, REG_H, REG_B);   break;
        case 0x61: ld_r8_r8(cpu, REG_H, REG_C);   break;
        case 0x62: ld_r8_r8(cpu, REG_H, REG_D);   break;
        case 0x63: ld_r8_r8(cpu, REG_H, REG_E);   break;
        case 0x64: ld_r8_r8(cpu, REG_H, REG_H);   break;
        case 0x65: ld_r8_r8(cpu, REG_H, REG_L);   break;
        case 0x66: ld_r8_hl(cpu, REG_H);          break;
        case 0x67: ld_r8_r8(cpu, REG_H, REG_A);   break;
        case 0x68: ld_r8_r8(cpu, REG_L, REG_B);   break;
        case 0x69: ld_r8_r8(cpu, REG_L, REG_C);   break;
        case 0x6A: ld_r8_r8(cpu, REG_L, REG_D);   break;
        case 0x6B: ld_r8_r8(cpu, REG_L, REG_E);   break;
        case 0x6C: ld_r8_r8(cpu, REG_L, REG_H);   break;
        case 0x6D: ld_r8_r8(cpu, REG_L, REG_L);   break;
        case 0x6E: ld_r8_hl(cpu, REG_L);          break;
        case 0x6F: ld_r8_r8(cpu, REG_L, REG_A);   break;
        case 0x70: ld_hl_r8(cpu, REG_B);          break;
        case 0x71: ld_hl_r8(cpu, REG_C);          break;
        case 0x72: ld_hl_r8(cpu, REG_D);          break;
        case 0x73: ld_hl_r8(cpu, REG_E);          break;
        case 0x74: ld_hl_r8(cpu, REG_H);          break;
        case 0x75: ld_hl_r8(cpu, REG_L);          break;
        case 0x76: halt(cpu);                     break;
        case 0x77: ld_hl_r8(cpu, REG_A) ;         break;
        case 0x78: ld_r8_r8(cpu, REG_A, REG_B);   break;
        case 0x79: ld_r8_r8(cpu, REG_A, REG_C);   break;
        case 0x7A: ld_r8_r8(cpu, REG_A, REG_D);   break;
        case 0x7B: ld_r8_r8(cpu, REG_A, REG_E);   break;
        case 0x7C: ld_r8_r8(cpu, REG_A, REG_H);   break;
        case 0x7D: ld_r8_r8(cpu, REG_A, REG_L);   break;
        case 0x7E: ld_r8_hl(cpu, REG_A);          break;
        case 0x7F: ld_r8_r8(cpu, REG_A, REG_A);   break;
        case 0x80: add_a_r8(cpu, REG_B);          break;
        case 0x81: add_a_r8(cpu, REG_C);          break;
        case 0x82: add_a_r8(cpu, REG_D);          break;
        case 0x83: add_a_r8(cpu, REG_E);          break;
        case 0x84: add_a_r8(cpu, REG_H);          break;
        case 0x85: add_a_r8(cpu, REG_L);          break;
        case 0x86: add_a_hl(cpu);                 break;
        case 0x87: add_a_r8(cpu, REG_A);          break;
        case 0x88: adc_a_r8(cpu, REG_B);          break;
        case 0x89: adc_a_r8(cpu, REG_C);          break;
        case 0x8A: adc_a_r8(cpu, REG_D);          break;
        case 0x8B: adc_a_r8(cpu, REG_E);          break;
        case 0x8C: adc_a_r8(cpu, REG_H);          break;
        case 0x8D: adc_a_r8(cpu, REG_L);          break;
        case 0x8E: adc_a_hl(cpu);                 break;
        case 0x8F: adc_a_r8(cpu, REG_A);          break;
        case 0x90: sub_a_r8(cpu, REG_B);          break;
        case 0x91: sub_a_r8(cpu, REG_C);          break;
        case 0x92: sub_a_r8(cpu, REG_D);          break;
        case 0x93: sub_a_r8(cpu, REG_E);          break;
        case 0x94: sub_a_r8(cpu, REG_H);          break;
        case 0x95: sub_a_r8(cpu, REG_L);          break;
        case 0x96: sub_a_hl(cpu);                 break;
        case 0x97: sub_a_r8(cpu, REG_A);          break;
        case 0x98: sbc_a_r8(cpu, REG_B);          break;
        case 0x99: sbc_a_r8(cpu, REG_C);          break;
        case 0x9A: sbc_a_r8(cpu, REG_D);          break;
        case 0x9B: sbc_a_r8(cpu, REG_E);          break;
        case 0x9C: sbc_a_r8(cpu, REG_H);          break;
        case 0x9D: sbc_a_r8(cpu, REG_L);          break;
        case 0x9E: sbc_a_hl(cpu);                 break;
        case 0x9F: sbc_a_r8(cpu, REG_A);          break;
        case 0xA0: and_a_r8(cpu, REG_B);          break;
        case 0xA1: and_a_r8(cpu, REG_C);          break;
        case 0xA2: and_a_r8(cpu, REG_D);          break;
        case 0xA3: and_a_r8(cpu, REG_E);          break;
        case 0xA4: and_a_r8(cpu, REG_H);          break;
        case 0xA5: and_a_r8(cpu, REG_L);          break;
        case 0xA6: and_a_hl(cpu);                 break;
        case 0xA7: and_a_r8(cpu, REG_A);          break;
        case 0xA8: xor_a_r8(cpu, REG_B);          break;
        case 0xA9: xor_a_r8(cpu, REG_C);          break;
        case 0xAA: xor_a_r8(cpu, REG_D);          break;
        case 0xAB: xor_a_r8(cpu, REG_E);          break;
        case 0xAC: xor_a_r8(cpu, REG_H);          break;
        case 0xAD: xor_a_r8(cpu, REG_L);          break;
        case 0xAE: xor_a_hl(cpu);                 break;
        case 0xAF: xor_a_r8(cpu, REG_A);          break;
        case 0xB0: or_r8(cpu, REG_B);             break;
        case 0xB1: or_r8(cpu, REG_C);             break;
        case 0xB2: or_r8(cpu, REG_D);             break;
        case 0xB3: or_r8(cpu, REG_E);             break;
        case 0xB4: or_r8(cpu, REG_H);             break;
        case 0xB5: or_r8(cpu, REG_L);             break;
        case 0xB6: or_hl(cpu);                    break;
        case 0xB7: or_r8(cpu, REG_A);             break;
        case 0xB8: cp_a_r8(cpu, REG_B);           break;
        case 0xB9: cp_a_r8(cpu, REG_C);           break;
        case 0xBA: cp_a_r8(cpu, REG_D);           break;
        case 0xBB: cp_a_r8(cpu, REG_E);           break;
        case 0xBC: cp_a_r8(cpu, REG_H);           break;
        case 0xBD: cp_a_r8(cpu, REG_L);           break;
        case 0xBE: cp_a_hl(cpu);                  break;
        case 0xBF: cp_a_r8(cpu, REG_A);           break;
        case 0xC0: ret_cond(cpu, COND_NZ);        break;
        case 0xC1: pop_r16stk(cpu, REG_BC);       break;
        case 0xC2: jp_cond_imm16(cpu, COND_NZ);   break;
        case 0xC3: jp_imm16(cpu);                 break;
        case 0xC4: call_cond_imm16(cpu, COND_NZ); break;
        case 0xC5: push_r16stk(cpu, REG_BC);      break;
        case 0xC6: add_a_imm8(cpu);               break;
        case 0xC7: rst_tgt3(cpu, 0x00);           break;
        case 0xC8: ret_cond(cpu, COND_Z);         break;
        case 0xC9: ret(cpu);                      break;
        case 0xCA: jp_cond_imm16(cpu, COND_Z);    break;
        case 0xCB: dispatch_prefixed_op(cpu);     break;
        case 0xCC: call_cond_imm16(cpu, COND_Z);  break;
        case 0xCD: call_imm16(cpu);               break;
        case 0xCE: adc_a_imm8(cpu);               break;
        case 0xCF: rst_tgt3(cpu, 0x08);           break;
        case 0xD0: ret_cond(cpu, COND_NC);        break;
        case 0xD1: pop_r16stk(cpu, REG_DE);       break;
        case 0xD2: jp_cond_imm16(cpu, COND_NC);   break;
        case 0xD3: invalid(cpu);                  break;
        case 0xD4: call_cond_imm16(cpu, COND_NC); break;
        case 0xD5: push_r16stk(cpu, REG_DE);      break;
        case 0xD6: sub_a_imm8(cpu);               break;
        case 0xD7: rst_tgt3(cpu, 0x10);           break;
        case 0xD8: ret_cond(cpu, COND_C);         break;
        case 0xD9: reti(cpu);                     break;
        case 0xDA: jp_cond_imm16(cpu, COND_C);    break;
        case 0xDB: invalid(cpu);                  break;
        case 0xDC: call_cond_imm16(cpu, COND_C);  break;
        case 0xDD: invalid(cpu);                  break;
        case 0xDE: sbc_a_imm8(cpu);               break;
        case 0xDF: rst_tgt3(cpu, 0x18);           break;
        case 0xE0: ldh_imm8_a(cpu);               break;
        case 0xE1: pop_r16stk(cpu, REG_HL);       break;
        case 0xE2: ldh_c_a(cpu);                  break;
        case 0xE3: invalid(cpu);                  break;
        case 0xE4: invalid(cpu);                  break;
        case 0xE5: push_r16stk(cpu, REG_HL);      break;
        case 0xE6: and_a_imm8(cpu);               break;
        case 0xE7: rst_tgt3(cpu, 0x20);           break;
        case 0xE8: add_sp_imm8(cpu);              break;
        case 0xE9: jp_hl(cpu);                    break;
        case 0xEA: ldh_imm16_a(cpu);              break;
        case 0xEB: invalid(cpu);                  break;
        case 0xEC: invalid(cpu);                  break;
        case 0xED: invalid(cpu);                  break;
        case 0xEE: xor_a_imm8(cpu);               break;
        case 0xEF: rst_tgt3(cpu, 0x28);           break;
        case 0xF0: ldh_a_imm8(cpu);               break;
        case 0xF1: pop_r16stk(cpu, REG_AF);       break;
        case 0xF2: ldh_a_c(cpu);                  break;
        case 0xF3: di(cpu);                       break;
        case 0xF4: invalid(cpu);                  break;
        case 0xF5: push_r16stk(cpu, REG_AF);      break;
        case 0xF6: or_imm8(cpu);                break;
        case 0xF7: rst_tgt3(cpu, 0x30);           break;
        case 0xF8: ld_hl_sp_imm8(cpu);            break;
        case 0xF9: ld_sp_hl(cpu);                 break;
        case 0xFA: ld_a_imm16(cpu);               break;
        case 0xFB: ei(cpu);                       break;
        case 0xFC: invalid(cpu);                  break;
        case 0xFD: invalid(cpu);                  break;
        case 0xFE: cp_a_imm8(cpu);                break;
        case 0xFF: rst_tgt3(cpu, 0x38);           break;
        }
}

static void dispatch_prefixed_op(struct cpu *cpu) {
        u8 op = cpu_fetch(cpu);
        switch (op) {
        case 0x00: rlc_r8(cpu, REG_B);       break;
        case 0x01: rlc_r8(cpu, REG_C);       break;
        case 0x02: rlc_r8(cpu, REG_D);       break;
        case 0x03: rlc_r8(cpu, REG_E);       break;
        case 0x04: rlc_r8(cpu, REG_H);       break;
        case 0x05: rlc_r8(cpu, REG_L);       break;
        case 0x06: rlc_hl(cpu);              break;
        case 0x07: rlc_r8(cpu, REG_A);       break;
        case 0x08: rrc_r8(cpu, REG_B);       break;
        case 0x09: rrc_r8(cpu, REG_C);       break;
        case 0x0A: rrc_r8(cpu, REG_D);       break;
        case 0x0B: rrc_r8(cpu, REG_E);       break;
        case 0x0C: rrc_r8(cpu, REG_H);       break;
        case 0x0D: rrc_r8(cpu, REG_L);       break;
        case 0x0E: rrc_hl(cpu);              break;
        case 0x0F: rrc_r8(cpu, REG_A);       break;
        case 0x10: rl_r8(cpu, REG_B);        break;
        case 0x11: rl_r8(cpu, REG_C);        break;
        case 0x12: rl_r8(cpu, REG_D);        break;
        case 0x13: rl_r8(cpu, REG_E);        break;
        case 0x14: rl_r8(cpu, REG_H);        break;
        case 0x15: rl_r8(cpu, REG_L);        break;
        case 0x16: rl_hl(cpu);               break;
        case 0x17: rl_r8(cpu, REG_A);        break;
        case 0x18: rr_r8(cpu, REG_B);        break;
        case 0x19: rr_r8(cpu, REG_C);        break;
        case 0x1A: rr_r8(cpu, REG_D);        break;
        case 0x1B: rr_r8(cpu, REG_E);        break;
        case 0x1C: rr_r8(cpu, REG_H);        break;
        case 0x1D: rr_r8(cpu, REG_L);        break;
        case 0x1E: rr_hl(cpu);               break;
        case 0x1F: rr_r8(cpu, REG_A);        break;
        case 0x20: sla_r8(cpu, REG_B);       break;
        case 0x21: sla_r8(cpu, REG_C);       break;
        case 0x22: sla_r8(cpu, REG_D);       break;
        case 0x23: sla_r8(cpu, REG_E);       break;
        case 0x24: sla_r8(cpu, REG_H);       break;
        case 0x25: sla_r8(cpu, REG_L);       break;
        case 0x26: sla_hl(cpu);              break;
        case 0x27: sla_r8(cpu, REG_A);       break;
        case 0x28: sra_r8(cpu, REG_B);       break;
        case 0x29: sra_r8(cpu, REG_C);       break;
        case 0x2A: sra_r8(cpu, REG_D);       break;
        case 0x2B: sra_r8(cpu, REG_E);       break;
        case 0x2C: sra_r8(cpu, REG_H);       break;
        case 0x2D: sra_r8(cpu, REG_L);       break;
        case 0x2E: sra_hl(cpu);              break;
        case 0x2F: sra_r8(cpu, REG_A);       break;
        case 0x30: swap_r8(cpu, REG_B);      break;
        case 0x31: swap_r8(cpu, REG_C);      break;
        case 0x32: swap_r8(cpu, REG_D);      break;
        case 0x33: swap_r8(cpu, REG_E);      break;
        case 0x34: swap_r8(cpu, REG_H);      break;
        case 0x35: swap_r8(cpu, REG_L);      break;
        case 0x36: swap_hl(cpu);             break;
        case 0x37: swap_r8(cpu, REG_A);      break;
        case 0x38: srl_r8(cpu, REG_B);       break;
        case 0x39: srl_r8(cpu, REG_C);       break;
        case 0x3A: srl_r8(cpu, REG_D);       break;
        case 0x3B: srl_r8(cpu, REG_E);       break;
        case 0x3C: srl_r8(cpu, REG_H);       break;
        case 0x3D: srl_r8(cpu, REG_L);       break;
        case 0x3E: srl_hl(cpu);              break;
        case 0x3F: srl_r8(cpu, REG_A);       break;
        case 0x40: bit_b3_r8(cpu, 0, REG_B); break;
        case 0x41: bit_b3_r8(cpu, 0, REG_C); break;
        case 0x42: bit_b3_r8(cpu, 0, REG_D); break;
        case 0x43: bit_b3_r8(cpu, 0, REG_E); break;
        case 0x44: bit_b3_r8(cpu, 0, REG_H); break;
        case 0x45: bit_b3_r8(cpu, 0, REG_L); break;
        case 0x46: bit_b3_hl(cpu, 0);        break;
        case 0x47: bit_b3_r8(cpu, 0, REG_A); break;
        case 0x48: bit_b3_r8(cpu, 1, REG_B); break;
        case 0x49: bit_b3_r8(cpu, 1, REG_C); break;
        case 0x4A: bit_b3_r8(cpu, 1, REG_D); break;
        case 0x4B: bit_b3_r8(cpu, 1, REG_E); break;
        case 0x4C: bit_b3_r8(cpu, 1, REG_H); break;
        case 0x4D: bit_b3_r8(cpu, 1, REG_L); break;
        case 0x4E: bit_b3_hl(cpu, 1);        break;
        case 0x4F: bit_b3_r8(cpu, 1, REG_A); break;
        case 0x50: bit_b3_r8(cpu, 2, REG_B); break;
        case 0x51: bit_b3_r8(cpu, 2, REG_C); break;
        case 0x52: bit_b3_r8(cpu, 2, REG_D); break;
        case 0x53: bit_b3_r8(cpu, 2, REG_E); break;
        case 0x54: bit_b3_r8(cpu, 2, REG_H); break;
        case 0x55: bit_b3_r8(cpu, 2, REG_L); break;
        case 0x56: bit_b3_hl(cpu, 2);        break;
        case 0x57: bit_b3_r8(cpu, 2, REG_A); break;
        case 0x58: bit_b3_r8(cpu, 3, REG_B); break;
        case 0x59: bit_b3_r8(cpu, 3, REG_C); break;
        case 0x5A: bit_b3_r8(cpu, 3, REG_D); break;
        case 0x5B: bit_b3_r8(cpu, 3, REG_E); break;
        case 0x5C: bit_b3_r8(cpu, 3, REG_H); break;
        case 0x5D: bit_b3_r8(cpu, 3, REG_L); break;
        case 0x5E: bit_b3_hl(cpu, 3);        break;
        case 0x5F: bit_b3_r8(cpu, 3, REG_A); break;
        case 0x60: bit_b3_r8(cpu, 4, REG_B); break;
        case 0x61: bit_b3_r8(cpu, 4, REG_C); break;
        case 0x62: bit_b3_r8(cpu, 4, REG_D); break;
        case 0x63: bit_b3_r8(cpu, 4, REG_E); break;
        case 0x64: bit_b3_r8(cpu, 4, REG_H); break;
        case 0x65: bit_b3_r8(cpu, 4, REG_L); break;
        case 0x66: bit_b3_hl(cpu, 4);        break;
        case 0x67: bit_b3_r8(cpu, 4, REG_A); break;
        case 0x68: bit_b3_r8(cpu, 5, REG_B); break;
        case 0x69: bit_b3_r8(cpu, 5, REG_C); break;
        case 0x6A: bit_b3_r8(cpu, 5, REG_D); break;
        case 0x6B: bit_b3_r8(cpu, 5, REG_E); break;
        case 0x6C: bit_b3_r8(cpu, 5, REG_H); break;
        case 0x6D: bit_b3_r8(cpu, 5, REG_L); break;
        case 0x6E: bit_b3_hl(cpu, 5);        break;
        case 0x6F: bit_b3_r8(cpu, 5, REG_A); break;
        case 0x70: bit_b3_r8(cpu, 6, REG_B); break;
        case 0x71: bit_b3_r8(cpu, 6, REG_C); break;
        case 0x72: bit_b3_r8(cpu, 6, REG_D); break;
        case 0x73: bit_b3_r8(cpu, 6, REG_E); break;
        case 0x74: bit_b3_r8(cpu, 6, REG_H); break;
        case 0x75: bit_b3_r8(cpu, 6, REG_L); break;
        case 0x76: bit_b3_hl(cpu, 6);        break;
        case 0x77: bit_b3_r8(cpu, 6, REG_A); break;
        case 0x78: bit_b3_r8(cpu, 7, REG_B); break;
        case 0x79: bit_b3_r8(cpu, 7, REG_C); break;
        case 0x7A: bit_b3_r8(cpu, 7, REG_D); break;
        case 0x7B: bit_b3_r8(cpu, 7, REG_E); break;
        case 0x7C: bit_b3_r8(cpu, 7, REG_H); break;
        case 0x7D: bit_b3_r8(cpu, 7, REG_L); break;
        case 0x7E: bit_b3_hl(cpu, 7);        break;
        case 0x7F: bit_b3_r8(cpu, 7, REG_A); break;
        case 0x80: res_b3_r8(cpu, 0, REG_B); break;
        case 0x81: res_b3_r8(cpu, 0, REG_C); break;
        case 0x82: res_b3_r8(cpu, 0, REG_D); break;
        case 0x83: res_b3_r8(cpu, 0, REG_E); break;
        case 0x84: res_b3_r8(cpu, 0, REG_H); break;
        case 0x85: res_b3_r8(cpu, 0, REG_L); break;
        case 0x86: res_b3_hl(cpu, 0);        break;
        case 0x87: res_b3_r8(cpu, 0, REG_A); break;
        case 0x88: res_b3_r8(cpu, 1, REG_B); break;
        case 0x89: res_b3_r8(cpu, 1, REG_C); break;
        case 0x8A: res_b3_r8(cpu, 1, REG_D); break;
        case 0x8B: res_b3_r8(cpu, 1, REG_E); break;
        case 0x8C: res_b3_r8(cpu, 1, REG_H); break;
        case 0x8D: res_b3_r8(cpu, 1, REG_L); break;
        case 0x8E: res_b3_hl(cpu, 1);        break;
        case 0x8F: res_b3_r8(cpu, 1, REG_A); break;
        case 0x90: res_b3_r8(cpu, 2, REG_B); break;
        case 0x91: res_b3_r8(cpu, 2, REG_C); break;
        case 0x92: res_b3_r8(cpu, 2, REG_D); break;
        case 0x93: res_b3_r8(cpu, 2, REG_E); break;
        case 0x94: res_b3_r8(cpu, 2, REG_H); break;
        case 0x95: res_b3_r8(cpu, 2, REG_L); break;
        case 0x96: res_b3_hl(cpu, 2);        break;
        case 0x97: res_b3_r8(cpu, 2, REG_A); break;
        case 0x98: res_b3_r8(cpu, 3, REG_B); break;
        case 0x99: res_b3_r8(cpu, 3, REG_C); break;
        case 0x9A: res_b3_r8(cpu, 3, REG_D); break;
        case 0x9B: res_b3_r8(cpu, 3, REG_E); break;
        case 0x9C: res_b3_r8(cpu, 3, REG_H); break;
        case 0x9D: res_b3_r8(cpu, 3, REG_L); break;
        case 0x9E: res_b3_hl(cpu, 3);        break;
        case 0x9F: res_b3_r8(cpu, 3, REG_A); break;
        case 0xA0: res_b3_r8(cpu, 4, REG_B); break;
        case 0xA1: res_b3_r8(cpu, 4, REG_C); break;
        case 0xA2: res_b3_r8(cpu, 4, REG_D); break;
        case 0xA3: res_b3_r8(cpu, 4, REG_E); break;
        case 0xA4: res_b3_r8(cpu, 4, REG_H); break;
        case 0xA5: res_b3_r8(cpu, 4, REG_L); break;
        case 0xA6: res_b3_hl(cpu, 4);        break;
        case 0xA7: res_b3_r8(cpu, 4, REG_A); break;
        case 0xA8: res_b3_r8(cpu, 5, REG_B); break;
        case 0xA9: res_b3_r8(cpu, 5, REG_C); break;
        case 0xAA: res_b3_r8(cpu, 5, REG_D); break;
        case 0xAB: res_b3_r8(cpu, 5, REG_E); break;
        case 0xAC: res_b3_r8(cpu, 5, REG_H); break;
        case 0xAD: res_b3_r8(cpu, 5, REG_L); break;
        case 0xAE: res_b3_hl(cpu, 5);        break;
        case 0xAF: res_b3_r8(cpu, 5, REG_A); break;
        case 0xB0: res_b3_r8(cpu, 6, REG_B); break;
        case 0xB1: res_b3_r8(cpu, 6, REG_C); break;
        case 0xB2: res_b3_r8(cpu, 6, REG_D); break;
        case 0xB3: res_b3_r8(cpu, 6, REG_E); break;
        case 0xB4: res_b3_r8(cpu, 6, REG_H); break;
        case 0xB5: res_b3_r8(cpu, 6, REG_L); break;
        case 0xB6: res_b3_hl(cpu, 6);        break;
        case 0xB7: res_b3_r8(cpu, 6, REG_A); break;
        case 0xB8: res_b3_r8(cpu, 7, REG_B); break;
        case 0xB9: res_b3_r8(cpu, 7, REG_C); break;
        case 0xBA: res_b3_r8(cpu, 7, REG_D); break;
        case 0xBB: res_b3_r8(cpu, 7, REG_E); break;
        case 0xBC: res_b3_r8(cpu, 7, REG_H); break;
        case 0xBD: res_b3_r8(cpu, 7, REG_L); break;
        case 0xBE: res_b3_hl(cpu, 7);        break;
        case 0xBF: res_b3_r8(cpu, 7, REG_A); break;
        case 0xC0: set_b3_r8(cpu, 0, REG_B); break;
        case 0xC1: set_b3_r8(cpu, 0, REG_C); break;
        case 0xC2: set_b3_r8(cpu, 0, REG_D); break;
        case 0xC3: set_b3_r8(cpu, 0, REG_E); break;
        case 0xC4: set_b3_r8(cpu, 0, REG_H); break;
        case 0xC5: set_b3_r8(cpu, 0, REG_L); break;
        case 0xC6: set_b3_hl(cpu, 0);        break;
        case 0xC7: set_b3_r8(cpu, 0, REG_A); break;
        case 0xC8: set_b3_r8(cpu, 1, REG_B); break;
        case 0xC9: set_b3_r8(cpu, 1, REG_C); break;
        case 0xCA: set_b3_r8(cpu, 1, REG_D); break;
        case 0xCB: set_b3_r8(cpu, 1, REG_E); break;
        case 0xCC: set_b3_r8(cpu, 1, REG_H); break;
        case 0xCD: set_b3_r8(cpu, 1, REG_L); break;
        case 0xCE: set_b3_hl(cpu, 1);        break;
        case 0xCF: set_b3_r8(cpu, 1, REG_A); break;
        case 0xD0: set_b3_r8(cpu, 2, REG_B); break;
        case 0xD1: set_b3_r8(cpu, 2, REG_C); break;
        case 0xD2: set_b3_r8(cpu, 2, REG_D); break;
        case 0xD3: set_b3_r8(cpu, 2, REG_E); break;
        case 0xD4: set_b3_r8(cpu, 2, REG_H); break;
        case 0xD5: set_b3_r8(cpu, 2, REG_L); break;
        case 0xD6: set_b3_hl(cpu, 2);        break;
        case 0xD7: set_b3_r8(cpu, 2, REG_A); break;
        case 0xD8: set_b3_r8(cpu, 3, REG_B); break;
        case 0xD9: set_b3_r8(cpu, 3, REG_C); break;
        case 0xDA: set_b3_r8(cpu, 3, REG_D); break;
        case 0xDB: set_b3_r8(cpu, 3, REG_E); break;
        case 0xDC: set_b3_r8(cpu, 3, REG_H); break;
        case 0xDD: set_b3_r8(cpu, 3, REG_L); break;
        case 0xDE: set_b3_hl(cpu, 3);        break;
        case 0xDF: set_b3_r8(cpu, 3, REG_A); break;
        case 0xE0: set_b3_r8(cpu, 4, REG_B); break;
        case 0xE1: set_b3_r8(cpu, 4, REG_C); break;
        case 0xE2: set_b3_r8(cpu, 4, REG_D); break;
        case 0xE3: set_b3_r8(cpu, 4, REG_E); break;
        case 0xE4: set_b3_r8(cpu, 4, REG_H); break;
        case 0xE5: set_b3_r8(cpu, 4, REG_L); break;
        case 0xE6: set_b3_hl(cpu, 4);        break;
        case 0xE7: set_b3_r8(cpu, 4, REG_A); break;
        case 0xE8: set_b3_r8(cpu, 5, REG_B); break;
        case 0xE9: set_b3_r8(cpu, 5, REG_C); break;
        case 0xEA: set_b3_r8(cpu, 5, REG_D); break;
        case 0xEB: set_b3_r8(cpu, 5, REG_E); break;
        case 0xEC: set_b3_r8(cpu, 5, REG_H); break;
        case 0xED: set_b3_r8(cpu, 5, REG_L); break;
        case 0xEE: set_b3_hl(cpu, 5);        break;
        case 0xEF: set_b3_r8(cpu, 5, REG_A); break;
        case 0xF0: set_b3_r8(cpu, 6, REG_B); break;
        case 0xF1: set_b3_r8(cpu, 6, REG_C); break;
        case 0xF2: set_b3_r8(cpu, 6, REG_D); break;
        case 0xF3: set_b3_r8(cpu, 6, REG_E); break;
        case 0xF4: set_b3_r8(cpu, 6, REG_H); break;
        case 0xF5: set_b3_r8(cpu, 6, REG_L); break;
        case 0xF6: set_b3_hl(cpu, 6);        break;
        case 0xF7: set_b3_r8(cpu, 6, REG_A); break;
        case 0xF8: set_b3_r8(cpu, 7, REG_B); break;
        case 0xF9: set_b3_r8(cpu, 7, REG_C); break;
        case 0xFA: set_b3_r8(cpu, 7, REG_D); break;
        case 0xFB: set_b3_r8(cpu, 7, REG_E); break;
        case 0xFC: set_b3_r8(cpu, 7, REG_H); break;
        case 0xFD: set_b3_r8(cpu, 7, REG_L); break;
        case 0xFE: set_b3_hl(cpu, 7);        break;
        case 0xFF: set_b3_r8(cpu, 7, REG_A); break;
        }
}
