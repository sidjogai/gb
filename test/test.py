import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.request

BLARGG_TESTS_URL  = "https://github.com/retrio/gb-test-roms/archive/c240dd7d700e5c0b00a7bbba52b53e4ee67b5f15.tar.gz"
MOONEYE_TESTS_URL = "https://gekkio.fi/files/mooneye-test-suite/mts-20240127-1204-74ae166/mts-20240127-1204-74ae166.tar.gz"

BLARGG_TESTS = [
    "blargg/cpu_instrs/cpu_instrs.gb",
    "blargg/instr_timing/instr_timing.gb",
    "blargg/mem_timing/mem_timing.gb",
    "blargg/mem_timing-2/mem_timing.gb",
]

MOONEYE_TESTS = [
    "mooneye/acceptance/add_sp_e_timing.gb",
    "mooneye/acceptance/call_cc_timing.gb",
    "mooneye/acceptance/call_cc_timing2.gb",
    "mooneye/acceptance/call_timing.gb",
    "mooneye/acceptance/call_timing2.gb",
    "mooneye/acceptance/di_timing-GS.gb",
    "mooneye/acceptance/div_timing.gb",
    "mooneye/acceptance/ei_sequence.gb",
    "mooneye/acceptance/ei_timing.gb",
    "mooneye/acceptance/halt_ime0_ei.gb",
    "mooneye/acceptance/halt_ime0_nointr_timing.gb",
    "mooneye/acceptance/halt_ime1_timing.gb",
    "mooneye/acceptance/halt_ime1_timing2-GS.gb",
    "mooneye/acceptance/if_ie_registers.gb",
    "mooneye/acceptance/intr_timing.gb",
    "mooneye/acceptance/jp_cc_timing.gb",
    "mooneye/acceptance/jp_timing.gb",
    "mooneye/acceptance/ld_hl_sp_e_timing.gb",
    "mooneye/acceptance/oam_dma_restart.gb",
    "mooneye/acceptance/oam_dma_start.gb",
    "mooneye/acceptance/oam_dma_timing.gb",
    "mooneye/acceptance/pop_timing.gb",
    "mooneye/acceptance/push_timing.gb",
    "mooneye/acceptance/rapid_di_ei.gb",
    "mooneye/acceptance/ret_cc_timing.gb",
    "mooneye/acceptance/ret_timing.gb",
    "mooneye/acceptance/reti_intr_timing.gb",
    "mooneye/acceptance/reti_timing.gb",
    "mooneye/acceptance/rst_timing.gb",

    "mooneye/acceptance/bits/reg_f.gb",

    "mooneye/acceptance/instr/daa.gb",

    "mooneye/acceptance/interrupts/ie_push.gb",

    "mooneye/acceptance/oam_dma/basic.gb",
    "mooneye/acceptance/oam_dma/reg_read.gb",
    "mooneye/acceptance/oam_dma/sources-GS.gb",

    "mooneye/acceptance/ppu/hblank_ly_scx_timing-GS.gb",
    "mooneye/acceptance/ppu/intr_1_2_timing-GS.gb",
    "mooneye/acceptance/ppu/intr_2_0_timing.gb",
    "mooneye/acceptance/ppu/intr_2_mode0_timing.gb",
    "mooneye/acceptance/ppu/intr_2_mode0_timing_sprites.gb",
    "mooneye/acceptance/ppu/intr_2_mode3_timing.gb",
    "mooneye/acceptance/ppu/intr_2_oam_ok_timing.gb",
    "mooneye/acceptance/ppu/lcdon_timing-GS.gb",
    "mooneye/acceptance/ppu/lcdon_write_timing-GS.gb",
    "mooneye/acceptance/ppu/stat_irq_blocking.gb",
    "mooneye/acceptance/ppu/stat_lyc_onoff.gb",
    "mooneye/acceptance/ppu/vblank_stat_intr-GS.gb",

    "mooneye/acceptance/timer/rapid_toggle.gb",
    "mooneye/acceptance/timer/tim00.gb",
    "mooneye/acceptance/timer/tim00_div_trigger.gb",
    "mooneye/acceptance/timer/tim01.gb",
    "mooneye/acceptance/timer/tim01_div_trigger.gb",
    "mooneye/acceptance/timer/tim10.gb",
    "mooneye/acceptance/timer/tim10_div_trigger.gb",
    "mooneye/acceptance/timer/tim11.gb",
    "mooneye/acceptance/timer/tim11_div_trigger.gb",
    "mooneye/acceptance/timer/tima_reload.gb",
    "mooneye/acceptance/timer/tima_write_reloading.gb",
    "mooneye/acceptance/timer/tma_write_reloading.gb",

    "mooneye/emulator-only/mbc1/bits_bank1.gb",
    "mooneye/emulator-only/mbc1/bits_bank2.gb",
    "mooneye/emulator-only/mbc1/bits_mode.gb",
    "mooneye/emulator-only/mbc1/bits_ramg.gb",
    "mooneye/emulator-only/mbc1/multicart_rom_8Mb.gb",
    "mooneye/emulator-only/mbc1/ram_256kb.gb",
    "mooneye/emulator-only/mbc1/ram_64kb.gb",
    "mooneye/emulator-only/mbc1/rom_16Mb.gb",
    "mooneye/emulator-only/mbc1/rom_1Mb.gb",
    "mooneye/emulator-only/mbc1/rom_2Mb.gb",
    "mooneye/emulator-only/mbc1/rom_4Mb.gb",
    "mooneye/emulator-only/mbc1/rom_512kb.gb",
    "mooneye/emulator-only/mbc1/rom_8Mb.gb",
]

def extract_url(url, dst):
    with tempfile.NamedTemporaryFile() as tar_file:
        urllib.request.urlretrieve(url, tar_file.name)
        with tempfile.TemporaryDirectory() as tmp_dir:
            shutil.unpack_archive(tar_file.name, tmp_dir, format="gztar")
            extracted_dir_path = f"{tmp_dir}/{os.listdir(tmp_dir)[0]}"
            os.rename(extracted_dir_path, dst)

def fetch_tests():
    tests = [(BLARGG_TESTS_URL, "blargg"),
             (MOONEYE_TESTS_URL, "mooneye")]
    for (url, dirname) in tests:
        if os.path.exists(dirname):
            print(f"'{dirname}' exists; skipping'")
        else:
            print(f"fetching '{dirname}' from '{url}'")
            extract_url(url, dirname)

def compile(cmd):
    result = subprocess.run(cmd, shell=True, stdout=subprocess.DEVNULL)
    if result.returncode != 0:
        sys.exit(1)

def run_tests(tests, quiet):
    npass = 0
    for name in tests:
        if quiet:
            result = subprocess.run(["./gb-test", name, "-q"],
                                    stdout=subprocess.DEVNULL,
                                    stderr=subprocess.DEVNULL)
        else:
            result = subprocess.run(["./gb-test", name],
                                    stdout=subprocess.DEVNULL,
                                    stderr=subprocess.DEVNULL)

        if result.returncode == 11:
            npass += 1
            msg = "\033[92mPASS\033[0m" if sys.stdout.isatty() else "PASS"
        elif result.returncode == 13:
            msg = "\033[91mFAIL\033[0m" if sys.stdout.isatty() else "FAIL"
        elif result.returncode == 15:
            msg = "\033[38;5;214mASSERT\033[0m" if sys.stdout.isatty() else "ASSERT"
        else:
            msg = "\033[7;91mCRASH\033[0m" if sys.stdout.isatty() else "CRASH"
        print(f"{name:<60}{' ':>10}{msg}")
    print(f"{npass} / {len(tests)} tests passed")

def main():
    if len(sys.argv) > 1 and sys.argv[1] == "fetch":
        fetch_tests()
        return

    search = sys.argv[1] if len(sys.argv) > 1 else ""
    tests  = [test for test in BLARGG_TESTS + MOONEYE_TESTS if search in test]

    compile("make")
    quiet = len(sys.argv) > 2 and sys.argv[2] == "-q"
    run_tests(tests, quiet)

if __name__ == "__main__":
    main()
