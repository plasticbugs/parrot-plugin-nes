# QuickNES (frank-nes fork), vendored

- Upstream: https://github.com/rh1tech/frank-nes, `src/core_quicknes/` and
  `src/emu2413/`, commit `7e6d0d9ec4500e246bc489cbc8eea0877c030040`
  (2026-05-12). Copied 2026-11-12.
- Origin: QuickNES (Nes_Emu 0.7.0) by Shay Green (blargg), via
  libretro/QuickNES_Core (compared against `26bb785c9deddb66a17717b21bb4e328f03ade32`).
  frank-nes changes about 700 lines of it for the RP2350: PRG and CHR used in
  place instead of copied, a hook to put the tile cache in PSRAM, MMC5 and
  NES 2.0 header fixes, Dendy timing, sprite limit, channel mutes, Game Genie.
- Licences: the core is LGPL-2.1-or-later (SPDX line in every file):
  `LICENSE`. emu2413 (YM2413, used for VRC7 sound, mapper 85) is Mitsutaka
  Okazaki's permissive notice: `LICENSE-emu2413`. The frank-nes repository as a
  whole is GPLv3 (`LICENSE-frank-nes`), but nothing kept here is frank-nes's
  own GPL code.

## Kept

- `core_quicknes/`: all of it (6502, PPU, APU with VRC6/VRC7/FME7/Namco
  extra sound, cart and state code, blip_buffer) and `mappers/`, 55 mappers:
  0-5, 7, 9-11, 15, 19, 21-26, 30, 32-34, 60, 66, 69-71, 73, 75, 78, 79,
  85-89, 93, 94, 97, 113, 140, 152, 154, 156, 180, 184, 190, 193, 206, 207,
  232, 240, 241, 244, 246.
- `emu2413/`: all of it.

## Left out

- `core_quicknes/quicknes.cpp`, `quicknes.h`: frank-nes's MIT glue between
  the core and its Pico platform (PSRAM, SD, HDMI, I2S).
- Everything else in frank-nes (`src/platform`, drivers, menu, FatFS, builds).

## Changes

Marked `MovieCart` in the source; everything else is byte-identical to
upstream (`diff -r`).

- `nes_ppu_rendering.h/.cpp`: a scroll log. With
  `Nes_Emu::set_scroll_log(buf)`, each drawn frame writes the fine X scroll
  and fine Y the background used on each line (2 bytes a line), so
  `nescv_bands()` can cut the picture along the NES's tile grid. Off (a
  null pointer test per background run) when not set.
- `nes_emu.h`: `apu()`, the `Nes_Apu` (its `save_state()` is what
  `nesapu.h` reads for the PSG), and `set_scroll_log()`.

## Building it

- Define `NO_UNALIGNED_ACCESS` (frank-nes's device build does). The PPU
  reads and writes 32 bits at odd addresses through `unaligned_uint32_t`
  (`blargg_common.h`); without the define that's a plain struct, and GCC
  merges pairs of those accesses into LDRD/STRD, which fault on an unaligned
  address on the M33. With it the struct is packed and GCC emits LDR/STR
  (the M33 does those unaligned). No speed cost.
- Six hot functions carry `__attribute__((section(".time_critical.x")))`
  (the Pico SDK places those in RAM). A non-Pico build needs
  `-Dsection(x)=used` or similar; `test/run.sh` does that for the Mac.
- 49 `printf` calls (load messages, mapper diagnostics) remain; the firmware
  has to provide `printf` or stub it.
- `-DQNES_EXT_TILE_CACHE` makes the PPU ask `qnes_get_tile_cache_buf(long*)`
  for its tile cache (2 x the CHR size, plus 128) before allocating it; that's
  how frank-nes puts it in PSRAM.

## Using it

- `Nes_Emu::load_ines_data()` keeps pointers into the ROM image for PRG and
  CHR (the comment in `nes_emu.h` saying PRG is copied is stale), so the image
  has to stay mapped (PSRAM) for as long as the game runs.
- Memory, measured on the Mac (`test/run.sh`): `Nes_Emu` 4,408 bytes, core
  heap after loading a CHR-RAM game 51,192 bytes (PPU 16.6 KB, tile cache
  16.5 KB, ...), plus the caller's picture buffer of
  `Nes_Emu::buffer_width` (272) x 242 = 65,824 bytes. CHR-ROM games add a tile
  cache of twice their CHR size.
- Picture: `set_pixels(buf, 272)`; after `emulate_frame()`, `frame().pixels`
  is 256 x 240 bytes at `frame().pitch` and each byte indexes
  `frame().palette[byte - frame().palette_begin]`, a NES colour 0-511 (low 6
  bits the colour, the 3 emphasis bits above). `emulate_skip_frame()` runs a
  frame without drawing; on the test ROMs skipping changes nothing.
- Sound: `Nes_Apu::save_state(apu_state_t*)` returns $4000-$4013, $4015,
  $4017, length counters and envelopes; `apu()` (above) reaches it.
