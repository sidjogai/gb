static void oam_dma(struct ppu *ppu);

static void tick_cpu(struct cpu *cpu)
{
        sync_timer(cpu->mem->timer, &cpu->interrupt_flag);
        oam_dma(cpu->mem->ppu);

        tick_ppu(cpu->mem->ppu, &cpu->interrupt_flag);
        tick_ppu(cpu->mem->ppu, &cpu->interrupt_flag);
        tick_ppu(cpu->mem->ppu, &cpu->interrupt_flag);
        tick_ppu(cpu->mem->ppu, &cpu->interrupt_flag);
}

static void cpu_write(struct cpu *cpu, u16 addr, u8 v)
{
        write_mem(cpu->mem, v, addr);
        tick_cpu(cpu);
}

static u8 cpu_read(struct cpu *cpu, u16 addr)
{
        u8 v = read_mem(cpu->mem, addr);
        tick_cpu(cpu);
        return v;
}

static u8 cpu_fetch(struct cpu *cpu)
{
        u8 v = read_mem(cpu->mem, cpu->regs.pc++);
        tick_cpu(cpu);
        return v;
}

enum reg {
        REG_A, REG_F, REG_AF,
        REG_B, REG_C, REG_BC,
        REG_D, REG_E, REG_DE,
        REG_H, REG_L, REG_HL,
        REG_SP,
};

#define valid_reg(r) ((r) >= 0 && (r) < 13)

static const char * reg_name(enum reg r)
{
        assert(valid_reg(r));
        static const char * const names[] = {
                [REG_A] ="a" ,  [REG_F]="f" ,  [REG_B]="b" ,  [REG_C]="c" ,
                [REG_D] ="d" ,  [REG_E]="e" ,  [REG_H]="h" ,  [REG_L]="l" ,
                [REG_AF]="af", [REG_BC]="bc", [REG_DE]="de", [REG_HL]="hl",
                [REG_SP]="sp",
        };
        return names[r];
}

static int reg_offset(enum reg r)
{
        assert(valid_reg(r));
        switch (r) {
        case REG_A:  return offsetof(struct registers, a);
        case REG_F:  return offsetof(struct registers, f);
        case REG_B:  return offsetof(struct registers, b);
        case REG_C:  return offsetof(struct registers, c);
        case REG_D:  return offsetof(struct registers, d);
        case REG_E:  return offsetof(struct registers, e);
        case REG_H:  return offsetof(struct registers, h);
        case REG_L:  return offsetof(struct registers, l);
        case REG_AF: return offsetof(struct registers, af);
        case REG_BC: return offsetof(struct registers, bc);
        case REG_DE: return offsetof(struct registers, de);
        case REG_HL: return offsetof(struct registers, hl);
        case REG_SP: return offsetof(struct registers, sp);
        }
}

static u8 getreg8(struct cpu *cpu, enum reg r)
{
        return *(u8 *)((char *)&cpu->regs + reg_offset(r));
}

static void setreg8(struct cpu *cpu, enum reg r, u8 v)
{
        *(u8 *)((char *)&cpu->regs + reg_offset(r)) = v;
}

static u16 getreg16(struct cpu *cpu, enum reg r)
{
        return *(u16 *)((char *)&cpu->regs + reg_offset(r));
}

static void setreg16(struct cpu *cpu, enum reg r, u16 v)
{
        *(u16 *)((char *)&cpu->regs + reg_offset(r)) = v;
}

static void setflag_z(struct cpu *cpu, bool v)
{
        cpu->regs.f = (u8)((cpu->regs.f & 0x7F) | v << 7);
}

static void setflag_n(struct cpu *cpu, bool v)
{
        cpu->regs.f = (u8)((cpu->regs.f & 0xBF) | v << 6);
}

static void setflag_h(struct cpu *cpu, bool v)
{
        cpu->regs.f = (u8)((cpu->regs.f & 0xDF) | v << 5);
}

static void setflag_c(struct cpu *cpu, bool v)
{
        cpu->regs.f = (u8)((cpu->regs.f & 0xEF) | v << 4);
}

static bool isflagset_z(struct cpu *cpu)
{
        return cpu->regs.f & 0x80;
}

static bool isflagset_n(struct cpu *cpu)
{
        return cpu->regs.f & 0x40;
}

static bool isflagset_h(struct cpu *cpu)
{
        return cpu->regs.f & 0x20;
}

static bool isflagset_c(struct cpu *cpu)
{
        return cpu->regs.f & 0x10;
}

enum cond {
        COND_Z, COND_NZ,
        COND_C, COND_NC,
};

#define valid_cond(c) ((c) >= 0 && (c) < 4)

static const char * cond_name(enum cond c)
{
        assert(valid_cond(c));
        static const char * const names[] = {
                [COND_Z]="z", [COND_NZ]="nz", [COND_C]="c", [COND_NC]="nc"
        };
        return names[c];
}

static bool checkcond(struct cpu *cpu, enum cond c)
{
        assert(valid_cond(c));
        switch (c) {
        case COND_Z:  return isflagset_z(cpu);
        case COND_NZ: return !isflagset_z(cpu);
        case COND_C:  return isflagset_c(cpu);
        case COND_NC: return !isflagset_c(cpu);
        }
}

static bool interrupt_pending(struct cpu *cpu)
{
        return cpu->interrupt_enable & cpu->interrupt_flag & 0x1F;
}

static void process_interrupts(struct cpu *cpu, u8 *op)
{
        if (!cpu->ime || !interrupt_pending(cpu))
                return;

        int bit;
        for (bit = 0; bit <= 4; bit++) {
                bool enabled   = (cpu->interrupt_enable >> bit) & 1;
                bool requested = (cpu->interrupt_flag >> bit) & 1;
                if (enabled && requested)
                        break;
        }

        cpu->interrupt_flag &= (u8)(0x1F & ~(1 << bit));
        cpu->ime = false;

        /* https://gist.github.com/SonoSooS/c0055300670d678b5ae8433e20bea595 */

        cpu->regs.pc--;
        tick_cpu(cpu);

        cpu->regs.sp--;
        tick_cpu(cpu);

        cpu_write(cpu, cpu->regs.sp--, cpu->regs.pc_msb);

        cpu_write(cpu, cpu->regs.sp, cpu->regs.pc_lsb);
        cpu->regs.pc = (u16)(0x40 + 8 * bit);

        *op = cpu_fetch(cpu);
}

#include "ops.c"
static void dispatch_op(struct cpu *cpu, u8 op);

static void step_cpu(struct cpu *cpu)
{
        if (cpu->halted) {
                if (interrupt_pending(cpu)) {
                        cpu->halted = false;
                        process_interrupts(cpu, &cpu->op);
                } else {
                        tick_cpu(cpu);
                        return;
                }
        }

        if (cpu->last_instr_was_ei) {
                cpu->ime = 1;
                cpu->last_instr_was_ei = false;
        }

        if (cpu->halt_bug) {
                cpu->regs.pc--;
                cpu->halt_bug = false;
        }

        dispatch_op(cpu, cpu->op);

        cpu->op = cpu_fetch(cpu);

        process_interrupts(cpu, &cpu->op);
}
