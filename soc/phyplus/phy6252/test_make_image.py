# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

import struct
import unittest

import make_image


class Segment(dict):
    def __init__(self, address, data, vaddr=None, memsz=None):
        super().__init__(p_type="PT_LOAD", p_filesz=len(data), p_memsz=memsz or len(data),
                         p_paddr=address, p_vaddr=address if vaddr is None else vaddr)
        self.contents = data

    def data(self):
        return self.contents


class Elf:
    def __init__(self, segments):
        self.segments = segments

    def iter_segments(self):
        return iter(self.segments)


class SymbolTable:
    def __init__(self, symbols):
        self.symbols = symbols

    def get_symbol_by_name(self, name):
        return [self.symbols[name]] if name in self.symbols else None


class ElfWithSymbols(Elf):
    def __init__(self, segments, symbols):
        super().__init__(segments)
        self.symbols = SymbolTable(symbols)

    def get_section_by_name(self, name):
        return self.symbols if name == ".symtab" else None


class ImageTest(unittest.TestCase):
    def test_rom_segment_layout(self):
        flash = struct.pack("<II", 0x1FFF2000, 0x11010109) + b"test"
        image = make_image.make_image(Elf([Segment(make_image.FLASH_BASE, flash)]))
        self.assertEqual(struct.unpack_from("<III", image), (2, 0xFFFFFFFF, 0x1FFF1838))
        addr, size, dest, crc = struct.unpack_from("<IIII", image, 0x100)
        self.assertEqual((addr, size, dest), (0x10100, len(flash), 0x11010100))
        self.assertEqual(crc, 0xFFFFFFFF)
        self.assertEqual(image[addr - 0x2000:addr - 0x2000 + size], flash)
        addr, size, dest, crc = struct.unpack_from("<IIII", image, 0x110)
        descriptor = image[addr - 0x2000:addr - 0x2000 + size]
        self.assertEqual(dest, 0x1FFF1838)
        self.assertEqual(descriptor, flash[:8])
        self.assertEqual(crc, 0xFFFFFFFF)

    def test_rom_loads_initialized_sram_segments(self):
        code = struct.pack("<II", 0x1FFF2000, 0x11010109) + b"code"
        ram_data = b"ramfunc"
        ram_load_address = make_image.FLASH_BASE + len(code)
        ram_run_address = make_image.RAM_START + 0x10
        image = make_image.make_image(Elf([
            Segment(make_image.FLASH_BASE, code),
            Segment(ram_load_address, ram_data, vaddr=ram_run_address),
        ]))

        self.assertEqual(struct.unpack_from("<I", image, 0)[0], 3)
        xip_src, xip_size, xip_dst, xip_crc = struct.unpack_from("<IIII", image, 0x100)
        self.assertEqual((xip_src, xip_size, xip_dst, xip_crc),
                         (0x10100, len(code), make_image.FLASH_BASE, 0xFFFFFFFF))
        ram_src, ram_size, ram_dst, ram_crc = struct.unpack_from("<IIII", image, 0x110)
        self.assertEqual((ram_src, ram_size, ram_dst, ram_crc),
                         (ram_load_address - make_image.FLASH_ORIGIN, len(ram_data),
                          ram_run_address, 0xFFFFFFFF))
        self.assertEqual(image[ram_src - make_image.IMAGE_OFFSET:
                               ram_src - make_image.IMAGE_OFFSET + ram_size], ram_data)
        desc_src, desc_size, desc_dst, desc_crc = struct.unpack_from("<IIII", image, 0x120)
        self.assertEqual(desc_dst, make_image.RUN_ADDRESS)
        self.assertEqual(desc_size, 8)
        self.assertEqual(desc_crc, 0xFFFFFFFF)
        self.assertEqual(image[desc_src - make_image.IMAGE_OFFSET:
                               desc_src - make_image.IMAGE_OFFSET + desc_size], code[:8])

    def test_rejects_out_of_range_and_overlapping_segments(self):
        flash = struct.pack("<II", 0x1FFF2000, 0x11010109)
        for extra in (Segment(0x11010104, b"bad"), Segment(0x11030000, b"bad")):
            with self.subTest(address=extra["p_paddr"]), self.assertRaises(ValueError):
                make_image.make_image(Elf([Segment(make_image.FLASH_BASE, flash), extra]))

    def test_rejects_invalid_reset_vector(self):
        for reset in (0x11010108, 0x11020001):
            with self.subTest(reset=reset), self.assertRaises(ValueError):
                flash = struct.pack("<II", 0x1FFF2000, reset)
                make_image.make_image(Elf([Segment(make_image.FLASH_BASE, flash)]))

    def test_rejects_flash_command_outside_initialized_ram(self):
        symbols = {"phy6252_flash_command": {"st_value": 0x11010101, "st_size": 16},
                   "phy6252_flash_wait": {"st_value": 0x1FFF1841, "st_size": 16}}
        with self.assertRaises(ValueError):
            make_image.validate_ram_functions(ElfWithSymbols([], symbols))

    def test_accepts_flash_commands_in_initialized_ram(self):
        symbols = {"phy6252_flash_command": {"st_value": 0x1FFF1841, "st_size": 16},
                   "phy6252_flash_wait": {"st_value": 0x1FFF1851, "st_size": 16}}
        segment = Segment(make_image.FLASH_BASE, bytes(32))
        segment["p_vaddr"] = make_image.RAM_START
        make_image.validate_ram_functions(ElfWithSymbols([segment], symbols))
        segment["p_filesz"] = 16
        with self.assertRaises(ValueError):
            make_image.validate_ram_functions(ElfWithSymbols([segment], symbols))

    def test_rejects_flash_driver_without_ram_functions(self):
        symbols = {"phy6252_flash_api": {"st_value": make_image.FLASH_BASE, "st_size": 4}}
        with self.assertRaises(ValueError):
            make_image.validate_ram_functions(ElfWithSymbols([], symbols))


if __name__ == "__main__":
    unittest.main()
