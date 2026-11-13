# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

import unittest

import merge_loader as m

LOADER = bytes(range(16)) * 0x400
APP = m.MCUBOOT_MAGIC + b"\x01\x02" * 0x100


class MergeLoader(unittest.TestCase):
    def test_slot_offset(self):
        image = m.merge(LOADER, APP)
        slot = m.SLOT0_OFFSET - m.IMAGE_OFFSET
        self.assertEqual(image[:len(LOADER)], LOADER)
        self.assertEqual(image[slot:], APP)
        self.assertEqual(set(image[len(LOADER):slot]), {0xFF})

    def test_loader_too_big(self):
        with self.assertRaises(ValueError):
            m.merge(b"\x00" * (m.SLOT0_OFFSET - m.IMAGE_OFFSET + 1), APP)

    def test_app_is_not_an_mcuboot_image(self):
        with self.assertRaises(ValueError):
            m.merge(LOADER, b"\x00" * 64)

    def test_app_too_big(self):
        with self.assertRaises(ValueError):
            m.merge(LOADER, APP + b"\x00" * m.SLOT0_SIZE)

    def test_arguments_swapped(self):
        with self.assertRaises(ValueError):
            m.merge(APP, LOADER)


if __name__ == "__main__":
    unittest.main()
