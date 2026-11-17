# What in this repository came from elsewhere

The plugin's own code, in `src/`, is under the GNU LGPL, version 2.1
(`LICENSE`). The copy of the cart's SDK in `sdk/` is under the MIT license
(`sdk/LICENSE`). These parts came from elsewhere and keep their own terms.

| Where | What | License |
|---|---|---|
| `vendor/quicknes/core_quicknes/` | QuickNES (Nes_Emu 0.7.0 by Shay Green, blargg), from the [frank-nes](https://github.com/rh1tech/frank-nes) fork, with the changes listed in `vendor/quicknes/VENDORED.md` | LGPL-2.1-or-later (`vendor/quicknes/LICENSE`) |
| `vendor/quicknes/emu2413/` | emu2413 by Mitsutaka Okazaki, changed for VRC7 by xodnizel | its own permissive notice (`vendor/quicknes/LICENSE-emu2413`) |
| `sdk/firmware/revc/sdk/tinyusb/` | [TinyUSB](https://github.com/hathach/tinyusb) (Ha Thach and contributors), with the cart's fixes, listed in its `VENDORED.md` | MIT (`sdk/firmware/revc/sdk/tinyusb/LICENSE`) |
| `cmake/pico_sdk_import.cmake` | from the [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) | BSD 3-Clause |

`vendor/quicknes/LICENSE-frank-nes` is the frank-nes repository's own
license, the GPL-3.0. None of frank-nes's own GPL code is kept here.

The built plugin also contains the Raspberry Pi Pico SDK's runtime (BSD
3-Clause) and TinyUSB (MIT).

The 8x8 screen font's letter shapes come from the 1982 arcade game Mr. Do!
