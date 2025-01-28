/* https://gbdev.io/pandocs/Power_Up_Sequence.html#cpu-registers */
static void skip_bootrom(struct gameboy *gb)
{
        struct cpu *cpu = &gb->cpu;
        /* TODO: flags register depends on value of header checksum */
        cpu->regs.af = 0x01B0;
        cpu->regs.bc = 0x0013;
        cpu->regs.de = 0x00D8;
        cpu->regs.hl = 0x014D;
        cpu->regs.pc = 0x0100;
        cpu->regs.sp = 0xFFFE;

        cpu->mem->div = 0xAB << 8;

        write_mem(cpu->mem, 0x00, TIMA_ADDR);
        write_mem(cpu->mem, 0x00, TMA_ADDR);
        write_mem(cpu->mem, 0xF8, TAC_ADDR);
        write_mem(cpu->mem, 0xE1, REG_IF_ADDR);
        write_mem(cpu->mem, 0x00, REG_IE_ADDR);

        write_mem(cpu->mem, 0x91, LCDC_ADDR);
        write_mem(cpu->mem, 0x85, STAT_ADDR);
        gb->ppu.dma = 0xFF;
        write_mem(cpu->mem, 0xFC, BGP_ADDR);
}

static void load_bootrom(struct gameboy *gb, const char *filename)
{
        FILE *fp = fopen(filename, "rb");
        if (!fp)
                die("Error opening boot ROM file '%s'\n", filename);
        fread(gb->bootrom, 1, 256, fp);
        gb->mem.bootrom_disabled = false;
        int error = ferror(fp);
        fclose(fp);
        if(error)
                die("Error loading boot ROM file '%s'\n", filename);
}

static void init_gb(struct gameboy *gb,
                    u32 *display_buf,
                    u32 *palette,
                    u8 *external_ram_buf,
                    u8 *rom_buf)
{
        memset(gb, 0, sizeof *gb);

        gb->cpu.mem     = &gb->mem;
        gb->ppu.mem     = &gb->mem;
        gb->mem.timer   = &gb->timer;
        gb->mem.ppu     = &gb->ppu;
        gb->mem.joypad  = &gb->joypad;
        gb->mem.mbc     = &gb->mbc;
        gb->mem.bootrom = gb->bootrom;

        gb->ppu.mode        = OAM_SCAN;
        gb->ppu.display_buf = display_buf;
        gb->ppu.vram_accessible = true;

        gb->mem.bootrom_disabled = true;

        gb->mbc.rom = rom_buf;
        gb->mbc.external_ram = external_ram_buf;
        gb->mbc.mbc1.bank1 = 1;

        memcpy(gb->ppu.palette, palette, sizeof gb->ppu.palette);
}

static enum mbc_type parse_mbc_type(u8 v)
{
        switch (v) {
        case 0x0: return NO_MBC;
        case 0x1: return MBC1;
        case 0x2: return MBC1; /* + RAM */
        case 0x3: return MBC1; /* + RAM + BATTERY */

        default: die("Unsupported MBC type %d\n", v);
        }
}

static int parse_rom_banks(u8 v)
{
        if (v > 0x8)
                die("Bad cartridge header: byte 0x148 = %d\n", v);
        return 1 << (v + 1);
}

static int parse_ram_banks(u8 v)
{
        switch (v) {
        case 0x00: return 0;
        case 0x02: return 1;
        case 0x03: return 4;
        case 0x04: return 16;
        case 0x05: return 8;
        default: die("Bad cartridge header: byte 0x149 = %d\n", v);
        }
}

static void load_rom(struct gameboy *gb, const char *path)
{
        SDL_RWops *f = SDL_RWFromFile(path, "rb");

        if (f == NULL)
                die("Error opening ROM '%s'\n", path);

        if (f->read(f, gb->mbc.rom, 32 * 1024, 1) != 1)
                die("Error reading ROM '%s'\n", path);

        gb->mbc.type      = parse_mbc_type(gb->mbc.rom[0x147]);
        gb->mbc.rom_banks = parse_rom_banks(gb->mbc.rom[0x148]);
        gb->mbc.ram_banks = parse_ram_banks(gb->mbc.rom[0x149]);

        if (gb->mbc.rom_banks - 2 > 0)  {
                size_t left = (size_t)((gb->mbc.rom_banks - 2) * 16 * 1024);
                if (f->read(f, gb->mbc.rom + 32 * 1024, left, 1) == 0)
                        die("Error reading! ROM '%s'\n", path);
        }
        f->close(f);
}
