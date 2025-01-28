static u8   mbc1_read_rom(struct mbc *, u16 u16);
static void mbc1_write_rom(struct mbc *, u8, u16);
static u8   mbc1_read_external_ram(struct mbc *, u16);
static void mbc1_write_external_ram(struct mbc *, u8, u16);

static u8 read_rom(struct mbc *m, u16 addr)
{
        switch (m->type) {
        case NO_MBC: 
                return m->rom[addr];
        case MBC1:   
                return mbc1_read_rom(m, addr);
        }
}

static void write_rom(struct mbc *m, u8 v, u16 addr)
{
        switch (m->type) {
        case NO_MBC:
                break;
        case MBC1:
                mbc1_write_rom(m, v, addr);
                break;
        }
}

static u8 read_external_ram(struct mbc *m, u16 addr)
{
        switch (m->type) {
        case NO_MBC:
                Q; 
                return 0;
        case MBC1: 
                return mbc1_read_external_ram(m, addr);
        }
}

static void write_external_ram(struct mbc *m, u8 v, u16 addr)
{
        switch (m->type) {
        case NO_MBC:
                Q;
                break;
        case MBC1:
                mbc1_write_external_ram(m, v, addr);
                break;
        }
}

/* ================================== MBC1 ================================== */

static void mbc1_write_rom(struct mbc *m, u8 v, u16 addr)
{
        if (addr <= 0x1FFF)
                m->mbc1.ram_enabled = (v & 0xF) == 0xA;
        else if (addr <= 0x3FFF) 
                m->mbc1.bank1 = ((v & 0x1F) == 0) ? 1 : (v & 0x1F);
        else if (addr <= 0x5FFF) 
                m->mbc1.bank2 = v & 0x3;
        else if (addr <= 0x7FFF) 
                m->mbc1.mode = v & 0x1;
}

static u8 mbc1_read_rom(struct mbc *m, u16 addr)
{
        u8 bank = 0;
        if (addr >= 0x4000)
                bank = m->mbc1.bank2 << 5 | m->mbc1.bank1;
        else if (m->mbc1.mode == 1)
                bank = m->mbc1.bank2 << 5;

        return m->rom[(bank & (m->rom_banks - 1)) << 14 | (addr & 0x3FFF)];
}

static u32 external_ram_phys_addr(struct mbc *m, u16 addr)
{
        u32 bank = m->mbc1.bank2 * (m->mbc1.mode == 1 && m->ram_banks == 4);
        return bank << 13 | (addr & 0x1FFF);
}

static void mbc1_write_external_ram(struct mbc *m, u8 v, u16 addr)
{
        if (m->mbc1.ram_enabled)
                m->external_ram[external_ram_phys_addr(m, addr)] = v;
}

static u8 mbc1_read_external_ram(struct mbc *m, u16 addr)
{
        if (!m->mbc1.ram_enabled)
                return 0xFF;

        return m->external_ram[external_ram_phys_addr(m, addr)];
}

