static void invalid_write(u8 v, u16 addr) {
        fprintf(stderr, "write of %.02x to %.04x discarded\n", v, addr);
}

static u8 invalid_read(u16 addr) {
        fprintf(stderr, "invalid read from addr %.04x; returning 0xFF\n", addr);
        return 0xFF;
}

static void write_mem(struct mem *mem, u8 v, u16 addr)
{
        if (addr <= 0x7FFF)
                write_rom(mem->mbc, v, addr);
        else if (addr <= 0x9FFF)
                write_vram(mem->ppu, v, addr);
        else if (addr <= 0xBFFF)
                write_external_ram(mem->mbc, v, addr);
        else if (addr <= 0xDFFF)
                mem->wram[addr - 0xC000] = v;
        else if (addr <= 0xFDFF)
                ; /* TODO: implement echo RAM */
        else if (addr <= 0xFE9F)
                write_oam(mem->ppu, v, addr);
        else if (addr <= 0xFEFF)
                ; /* TODO: implement not usable */
        else if (addr == 0xFF00)
                write_p1(mem->joypad, v);
        else if (addr == 0xFF01) {
                /* if (globals.show_serial_output) */
                /*         putc(v, stdout); */
        }
        else if (addr == 0xFF02)
                ;
        else if (addr < 0xFF04)
                invalid_write(v, addr);
        else if (addr <= 0xFF07)
                write_timer(mem->timer, v, addr);
        else if (addr < 0xFF0F)
                invalid_write(v, addr);
        else if (addr == 0xFF0F)
                mem->int_flag = v;
        else if (addr <= 0xFF26)
                ; /* TODO: audio */
        else if (addr < 0xFF30)
                invalid_write(v, addr);
        else if (addr <= 0xFF3F)
                ; /* TODO: wave pattern */
        else if (addr <= 0xFF49)
                write_ppu_reg(mem->ppu, v, addr);
        else if (addr == 0xFF50)
                mem->bootrom_disabled = v;
        else if (addr <= 0xFF4B)
                ; /* TODO: palette stuff */
        else if (addr <= 0xFF4F)
                ; /* TODO: check what to do here */
        else if (addr <= 0xFF50)
                ; /* TODO: non-zero to disable boot ROM */
        else if (addr <= 0xFF7F)
                ; /* TODO: check what to do here */
        else if (addr <= 0xFFFE)
                mem->hram[addr - HRAM_START] = v;
        else if (addr == 0xFFFF)
                mem->int_enable = v;
        else
                die("write_mem: invalid address $%.04x", addr);
}

static u8 read_mem(struct mem *mem, u16 addr)
{
        u8 v = 0xFF;
        if (addr <= 0x7FFF)
                v = read_rom(mem->mbc, addr);
        else if (addr <= 0x9FFF)
                v = read_vram(mem->ppu, addr);
        else if (addr <= 0xBFFF)
                v = read_external_ram(mem->mbc, addr);
        else if (addr <= 0xDFFF)
                v = mem->wram[addr - 0xC000];
        else if (addr <= 0xFDFF)
                ; // TODO implement echo RAM
        else if (addr <= 0xFE9F)
                v = read_oam(mem->ppu, addr);
        else if (addr <= 0xFEFF)
                ; // TODO implement not usable
        else if (addr == 0xFF00)
                v = read_p1(mem->joypad);
        else if (addr == 0xFF01)
                ;
        else if (addr == 0xFF02)
#ifdef MOONEYE
                v = 0xFF;
#else
        ;
#endif
        else if (addr < 0xFF04)
                invalid_read(addr);
        else if (addr <= 0xFF07)
                v = read_timer(mem->timer, addr);
        else if (addr < 0xFF0F)
                invalid_read(addr);
        else if (addr == 0xFF0F)
                /* bits 7-5 not wired so they read 1s */
                v = mem->int_flag | 0xE0;
        else if (addr <= 0xFF26)
                ; // TODO audio
        else if (addr < 0xFF30)
                invalid_read(addr);
        else if (addr <= 0xFF3F)
                ; // TODO wave pattern
        else if (addr <= 0xFF49)
                v = read_ppu_reg(mem->ppu, addr);
        else if (addr == 0xFF50)
                v = mem->bootrom_disabled;
        else if (addr <= 0xFF4B)
                ;
        else if (addr <= 0xFF4F)
                ; // TODO check what to do here
        else if (addr <= 0xFF50)
                ; // TODO non-zero to disable boot ROM
        else if (addr <= 0xFF7F)
                ; // TODO check what to do here
        else if (addr <= 0xFFFE)
                v = mem->hram[addr - HRAM_START];
        else if (addr == 0xFFFF)
                v = mem->int_enable | 0xE0;
        else
                die("read_mem: invalid address $%.04x", addr);
        return v;
}

