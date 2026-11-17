# NES

This plugin plays NES games on the ColecoVision.

## Setup

Copy `NES.CVP` into the `/PLUGINS` folder on the card. Put your `.nes` files in `/NES`, or anywhere else on the card. Pick one in the menu to play it.

USB pads need a powered hub on the cart's USB port, because the cart can't power them. A Switch Pro Controller, an 8BitDo pad in Switch mode, or a plain HID pad works.

## Controls

On the ColecoVision controller:

- The joystick is the D-pad.
- Left fire is B, and right fire is A.
- Keypad # is Start, and keypad * is Select.
- Keypad 2 and 8 move the view up and down by 8 lines.
- Keypad 5 puts the view back where it started.
- Keypad 0 switches between smooth scrolling and scrolling by tiles.
- Press RESET on the console to go back to the menu.

On a USB pad:

- The D-pad or the left stick is the D-pad.
- The right face button is A, and the bottom one is B, as on a Nintendo pad.
- Plus is Start, and minus is Select.
- L and R move the view up and down.
- ZL puts the view back where it started.
- ZR switches the scrolling.

The first pad plays along with the console's controller as player 1. A second pad is player 2.

## Notes

- The TV shows 192 of the NES's 240 lines. Use keypad 2, 8 and 5 to see the rest.
- Smooth scrolling moves the way the NES does. Scrolling by tiles moves 8 pixels at a time, but the picture updates more often.
- There are no battery saves or save states yet.
- Sampled (DMC) sound doesn't play.

## Credits

- QuickNES, from Nes_Emu 0.7.0 by Shay Green (blargg), in the frank-nes fork, under the LGPL-2.1-or-later. The source is at https://github.com/rh1tech/frank-nes
- emu2413 by Mitsutaka Okazaki, changed for VRC7 by xodnizel, under its own permissive license.
- TinyUSB by Ha Thach, under the MIT license. The source is at https://github.com/hathach/tinyusb
- The plugin itself is by Parrot Cart. Its source, with the changes made to QuickNES, is linked next to its download.
