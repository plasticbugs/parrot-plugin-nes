// nes -- NES games on the ColecoVision: pick a .nes on the card in the menu,
// press A, and the cart runs it (QuickNES) and sends each picture and the
// sound to the console as the Game Boy kernel's frame lists (nescv). RESET
// goes back to the menu.
//
//   core 1  serves the kernel and a list a TV frame (cvframes.c)
//   core 0  this loop: for each TV frame the console starts, one NES frame
//           (drawn only when the encoder can take a picture), its picture
//           and sound into the next list, published for the frame after
//
// The ROM is read from the card into the PSRAM, where QuickNES runs it in
// place (through the XIP cache: this plugin runs from SRAM, so the cache is
// the PSRAM's alone). QuickNES's tile cache goes there too when the heap
// hasn't room for it: a CHR ROM's past 8K (twice its size).
//
// The controller: joystick = D-pad, left fire = B, right fire = A, # =
// Start, * = Select. The picture is 192 of the NES's 240 lines at 1:1:
// keypad 2 and 8 move the window up and down, 5 puts it back. Keypad 0
// switches the scrolling: smooth (the background moves as the NES's does)
// or by tiles (cut along the NES's tile grid: 8 pixels at a time, but more
// pictures a second, as the console then holds most cells already).
//
// USB game pads (cvpad.h; a Switch Pro Controller or a plain HID pad,
// through a powered hub on the cart's USB port): the first is player 1,
// with the console's controller, the second player 2. D-pad or left stick
// = D-pad, the right face button = A and the bottom one = B (where
// Nintendo puts them), + = Start, - = Select; L and R move the window up
// and down, ZL puts it back, ZR switches the scrolling.
#include <cstdio>
#include <cstring>

#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "hardware/xip_cache.h"

extern "C" {
#include "cvdiag.h"
#include "cvframes.h"
#include "cvheap.h"
#include "cvpad.h"
#include "cvpsram.h"
#include "cvsd.h"
#include "vdp.h"
}
#include "gbkernel.h"
#include "handoff.h"
#include "nes_emu.h"
#include "nesapu.h"

#define OPEN_US     20000           // serve the console this long before the card's (blocking) start
#define WATCHDOG_MS 4000            // a hang (the emulator's) reboots into the menu
#define CROP_HOME   16              // the first NES line shown: keeps a status bar at the top
#define NOTE_US     1000000         // the menu's note is brought up to date this often

// the NES's picture buffer; before a game runs, a canvas for the messages
// and the card's file system (needed only to load the ROM: 13.7K the heap
// keeps, for QuickNES, since the USB host's code took as much)
static union {
    uint8_t nes[Nes_Emu::buffer_width * (Nes_Emu::image_height + 2)];
    struct {
        canvas_t msg;
        fs_t fs;
    } load;
} pic __attribute__((aligned(4)));
static_assert(sizeof pic == sizeof pic.nes, "the canvas and the file system fit the picture buffer");
static canvas_t &msg_canvas = pic.load.msg;

static nescv_t enc;
static nescv_psg_t tr;
static uint8_t list[NESCV_LIST_MAX];
static size_t list_len;
static bool list_pending;                   // built, not published yet (cvf_publish said wait)
static uint32_t seen_frames;
static uint64_t pic_index;
static uint8_t scroll_log[480];
static uint8_t lut[256], msg_lut[256], map512[512];
static fs_t &fs = pic.load.fs;           // (in pic: until the game starts)
static fs_file_t file;
static char name[FS_NAME], note[33];
static bool pic_index_staged;           // cvd_stage(8): the first picture offered
static uint32_t mem_left;               // the largest malloc still possible once the game had started
static bool cache_in_psram;             // QuickNES's tile cache: not on the heap

// QuickNES's fprintf(stderr, ...) (CMakeLists.txt: --wrap=fprintf): there
// is no stderr here, and newlib's printf behind it is 19K of the heap
extern "C" int __wrap_fprintf(FILE *, const char *, ...) { return 0; }

// ---- the console, a list a TV frame ------------------------------------------------

static void publish(void) {
    if (list_pending && cvf_publish(list, list_len)) list_pending = false;
}

// a new TV frame started since the last call (and the last list is out)
static bool new_frame(void) {
    cvpad_task();                           // (nothing, with cvpad's timer: TinyUSB 0.21)
    uint32_t t = time_us_32();
    cvf_task(t);
    watchdog_update();
    publish();
    if (list_pending) return false;
    uint32_t f = cvf_frames();
    if (f == seen_frames) return false;
    seen_frames = f;
    return true;
}

static void send(const uint8_t *psg, int npsg) {
    list_len = nescv_tv_frame(&enc, psg, npsg, list);
    list_pending = true;
    publish();
}

// ---- messages: a canvas as the picture ---------------------------------------------

static bool msg_waiting;

static void offer_msg(void) {
    nescv_picture_t p;
    p.pixels = &msg_canvas.px[0][0];
    p.pitch = VDP_W;
    p.lut = msg_lut;
    for (int b = 0; b < NESCV_ROWS; b++) {
        p.line[b] = (uint8_t)(8 * b);
        p.col[b] = 0;
    }
    p.backdrop = C_BLACK;
    msg_waiting = !nescv_offer(&enc, &p, ++pic_index);
}

static void centre(int y, const char *s, uint8_t colour, int scale) {
    int w = (int)strlen(s) * 8 * scale;
    canvas_text(&msg_canvas, w < VDP_W ? (VDP_W - w) / 2 : 0, y, s, colour, scale);
}

static int bar = -1;                        // loading: a bar this many percent full (-1: none)

static void message(const char *head, const char *l1, const char *l2) {
    canvas_init(&msg_canvas);
    centre(48, "NES", C_LRED, 3);
    if (name[0]) centre(88, name, C_WHITE, 1);
    if (head) centre(112, head, C_LYELLOW, 1);
    if (l1) centre(128, l1, C_GRAY, 1);
    if (l2) centre(140, l2, C_GRAY, 1);
    if (bar >= 0) {                         // loading (the font has no %)
        canvas_fill(&msg_canvas, 76, 128, 104, 8, C_GRAY);
        canvas_fill(&msg_canvas, 77, 129, 102, 6, C_BLACK);
        canvas_fill(&msg_canvas, 78, 130, bar, 4, C_WHITE);
    }
    centre(176, "RESET: BACK TO THE MENU", C_DBLUE, 1);
    offer_msg();
}

// serve the console, a list a frame, for us microseconds (forever: 0)
static void serve_for(uint32_t us) {
    uint32_t t0 = time_us_32();
    while (!us || time_us_32() - t0 < us) {
        if (!new_frame()) continue;
        if (msg_waiting) offer_msg();
        send(nullptr, 0);
    }
}

static void __attribute__((noreturn)) fail(const char *why, const char *more, const char *menu_note) {
    message("IT CAN'T RUN", why, more);
    cvf_note(menu_note);
    serve_for(0);
    for (;;) tight_loop_contents();
}

static const char *fs_why(int r) {
    switch (r) {
    case FS_EIO:     return cvsd_card_in() ? "THE CARD DIDN'T ANSWER" : "NO CARD IN THE SLOT";
    case FS_ENOFS:   return "NO FAT OR EXFAT ON THE CARD";
    case FS_ENOFILE: return "THE FILE IS EMPTY OR GONE";
    case FS_ECHAIN:  return "ITS CLUSTERS ARE BROKEN";
    case FS_EFRAG:   return "IT'S IN TOO MANY PIECES";
    default:         return "THE CARD'S FILE SYSTEM IS ODD";
    }
}

// ---- the ROM ------------------------------------------------------------------------

static uint32_t rom_size, chr_bytes;

// the file into the PSRAM, a few sectors between TV frames
static void load_rom(void) {
    static uint32_t sec[128];
    uint32_t n = (uint32_t)file.size, last_pc = 101;
    for (uint32_t at = 0; at < n; at += 512) {
        uint32_t lba = fs_lba(&fs, at / 512);
        if (!lba || !cvsd_read(lba, (uint8_t *)sec)) fail("A CARD READ FAILED", nullptr, "NES: a card read failed");
        volatile uint32_t *d = CVPSRAM_UNCACHED + at / 4;
        for (int i = 0; i < 128; i++) d[i] = sec[i];
        uint32_t pc = (uint32_t)((uint64_t)at * 100 / n);
        if (pc / 10 != last_pc / 10) {
            last_pc = pc;
            bar = (int)pc;
            message("LOADING", nullptr, nullptr);
        }
        if (new_frame()) {
            if (msg_waiting) offer_msg();
            send(nullptr, 0);
        }
    }
    xip_cache_invalidate_all();
    bar = -1;
    rom_size = n;
}

// QuickNES asks for its tile cache last, after everything else it takes.
// A CHR ROM's past 8K (twice its size, plus 128) goes to the PSRAM after
// the ROM, through the XIP cache, as it always has. CHR RAM's or an 8K
// ROM's (16,512 bytes) goes on the heap, the fastest place and where 0.2.0
// had it, while that leaves the heap CACHE_SPARE; if it ever doesn't, the
// PSRAM too: slower, but the game starts (0.3.x panicked here, out of
// memory). The menu's note says which: "mem" the heap, "psr" the PSRAM.
#ifndef CACHE_SPARE
#define CACHE_SPARE 2048
#endif

void *qnes_get_tile_cache_buf(long *size) {
    uint32_t want = chr_bytes > 0x2000 ? chr_bytes * 2 + 128 : 0x2000 * 2 + 128;
    if (chr_bytes <= 0x2000 && cvheap_largest() >= want + CACHE_SPARE) return nullptr;
    uint32_t at = (rom_size + 0xFFFF) & ~0xFFFFu;
    if (at + want > CVPSRAM_SIZE) return nullptr;   // (a ROM that fills the PSRAM: main() takes 4 MB at most)
    cache_in_psram = true;
    *size = (long)(CVPSRAM_SIZE - at);
    return CVPSRAM_CACHED + at;
}

// ---- the game -----------------------------------------------------------------------

static Nes_Emu *emu;
static int crop = CROP_HOME;
static int quantum = 1;                     // nescv_bands: 1 smooth, 8 by tiles
static uint8_t last_key = 0x0F;
static uint32_t late, shown_at_note, us_max;

static int pad_from(uint8_t joy, uint8_t fire, uint8_t key) {
    int pad = 0;                            // QuickNES: A 01 B 02 Select 04 Start 08 Up 10 Down 20 Left 40 Right 80
    if (!(joy & 1)) pad |= 0x10;
    if (!(joy & 2)) pad |= 0x80;
    if (!(joy & 4)) pad |= 0x20;
    if (!(joy & 8)) pad |= 0x40;
    if (!(fire & 1)) pad |= 0x02;
    if (!(fire & 2)) pad |= 0x01;
    if (key == 0x6) pad |= 0x08;            // #
    if (key == 0x9) pad |= 0x04;            // *
    return pad;
}

// a USB pad as QuickNES's buttons
static int pad_from_usb(const gamepad_t &g) {
    const uint32_t b = g.buttons, d = cvpad_dirs(&g);
    int pad = 0;
    if (d & GP_UP) pad |= 0x10;
    if (d & GP_DOWN) pad |= 0x20;
    if (d & GP_LEFT) pad |= 0x40;
    if (d & GP_RIGHT) pad |= 0x80;
    if (b & GP_EAST) pad |= 0x01;           // A
    if (b & GP_SOUTH) pad |= 0x02;          // B
    if (b & GP_MINUS) pad |= 0x04;          // Select
    if (b & GP_PLUS) pad |= 0x08;           // Start
    return pad;
}

static uint32_t last_usb;                   // pad 0's buttons, for the presses

static void window_key(int k) {             // 2, 8, 5: the window; 0: the scrolling
    if (k == 2) crop = crop > 8 ? crop - 8 : 0;
    else if (k == 8) crop = crop < 40 ? crop + 8 : 48;
    else if (k == 5) crop = CROP_HOME;
    else if (k == 0) quantum = quantum == 1 ? 8 : 1;
}

static void game_frame(void) {
    uint32_t t0 = time_us_32();
    uint8_t joy, fire, key;
    cvf_input(&joy, &fire, &key);
    if (key != last_key) {                  // on the press
        window_key(key == 0x7 ? 2 : key == 0x1 ? 8 : key == 0x3 ? 5 : key == 0xA ? 0 : -1);
        last_key = key;
    }
    int pad = pad_from(joy, fire, key), pad2 = 0;
    gamepad_t g;
    if (cvpad_get(0, &g)) {
        pad |= pad_from_usb(g);
        const uint32_t down = g.buttons & ~last_usb;
        if (down & GP_L) window_key(2);
        if (down & GP_R) window_key(8);
        if (down & GP_ZL) window_key(5);
        if (down & GP_ZR) window_key(0);
        last_usb = g.buttons;
    } else last_usb = 0;
    if (cvpad_get(1, &g)) pad2 = pad_from_usb(g);
    // both of a pair at once (the console's stick and a pad): neither, as on a real pad
    if ((pad & 0x30) == 0x30) pad &= ~0x30;
    if ((pad & 0xC0) == 0xC0) pad &= ~0xC0;
    bool want = nescv_wants_picture(&enc);
    if (want) {
        memset(scroll_log, 0xFF, sizeof scroll_log);
        emu->emulate_frame(pad, pad2);
        const Nes_Emu::frame_t &fr = emu->frame();
        for (int i = 0; i < 256; i++) {
            int k = i - fr.palette_begin;
            lut[i] = k >= 0 && k < fr.palette_size ? map512[fr.palette[k] & 511] : (uint8_t)C_BLACK;
        }
        nescv_picture_t p;
        p.pixels = fr.pixels - 8;           // the buffer's column 0: the picture starts at 8
        p.pitch = fr.pitch;
        p.lut = lut;
        p.backdrop = map512[fr.palette[0] & 511];
        nescv_bands(scroll_log, crop, quantum, &p);
        nescv_offer(&enc, &p, ++pic_index);
    } else {
        emu->emulate_skip_frame(pad, pad2);
    }
    nescv_apu_t a;
    nesapu_read(emu->apu(), &a);
    uint8_t psg[16];
    send(psg, nescv_psg(&tr, &a, psg));
    uint32_t us = time_us_32() - t0;
    if (us > us_max) us_max = us;
}

// the menu's bottom line, should RESET come: how it went (for the cart's
// tuning: pictures a second, frames the cart was late for, its slowest)
static void update_note(void) {
    static uint32_t t_last, late_last;
    uint32_t t = time_us_32();
    if (t - t_last < NOTE_US) return;
    uint32_t shown = enc.stats.shown + enc.stats.same;
    unsigned per_s = MIN(shown - shown_at_note, 99u), dl = MIN(late - late_last, 99u),   // 30 characters at most:
             ms = MIN(us_max / 1000, 99u), tenths = us_max / 100 % 10;                      // what the menu shows
    char n[64];
    if (cvpad_seen()) {                     // once a pad has been in: the longest cvpad_task(), in ms, instead of the memory
        unsigned pad = MIN(cvpad_task_max_us() / 10, 9999u);
        snprintf(n, sizeof n, "NES %s %u %uL %u.%ums pad %u.%02u", quantum == 1 ? "sm" : "ti", per_s, dl, ms, tenths,
                 pad / 100, pad % 100);
    } else {
        snprintf(n, sizeof n, "NES %s %u %uL %u.%ums %s %lu", quantum == 1 ? "sm" : "ti", per_s, dl, ms, tenths,
                 cache_in_psram ? "psr" : "mem", (unsigned long)mem_left);
    }
    cvf_note(n);
    t_last = t;
    late_last = late;
    shown_at_note = shown;
    us_max = 0;
}

int main(void) {
    static const cvf_config_t cfg = {gbkernel_bin, GBKERNEL_BIN_LEN, handoff_bin, HANDOFF_BIN_LEN};
    cvf_start(&cfg);
    cvd_start();                            // a fault, a hang or a panic: what, in the menu's note (cvdiag.h)
    cvd_stage(1);
    cvpad_init();                           // the pads find their way in while the game loads
    nescv_init(&enc);
    nescv_psg_init(&tr);
    for (int i = 0; i < 256; i++) msg_lut[i] = (i & 15) ? (uint8_t)(i & 15) : (uint8_t)C_BLACK;
    for (int i = 0; i < 512; i++) map512[i] = nescv_nes_colours[i & 63];

    const cvl_t *b = cvf_launch();
    if (b && b->path[0]) {
        const char *slash = strrchr(b->path, '/');
        snprintf(name, 33, "%.32s", slash ? slash + 1 : b->path);   // a line of the screen at most
        for (char *c = name; *c; c++)       // the font is upper case
            if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 'a' + 'A');
    }
    message(b && b->path[0] ? "STARTING" : nullptr, b && b->path[0] ? nullptr : "OPEN A .NES FILE FROM THE MENU", nullptr);
    serve_for(OPEN_US);
    if (!b || !b->path[0]) fail("OPEN A .NES FILE FROM THE MENU", nullptr, nullptr);

    int r = cvsd_open_launched(&fs, b, false, &file);  // (blocks up to 1.5 s: the last list runs again meanwhile)
    if (r != FS_OK) fail(fs_why(r), nullptr, "NES: the file couldn't be read");
    cvd_stage(2);
    if (!cvpsram_init()) fail("THE PSRAM DIDN'T ANSWER", nullptr, "NES: the PSRAM didn't answer");
    if (file.size < 16 || file.size > CVPSRAM_SIZE / 2) fail("IT ISN'T A NES ROM", nullptr, "NES: not a NES ROM");
    watchdog_enable(WATCHDOG_MS, true);
    cvd_stage(3);
    load_rom();
    cvd_stage(4);

    const uint8_t *rom = CVPSRAM_CACHED;
    if (memcmp(rom, "NES\x1A", 4)) fail("IT ISN'T A NES ROM", "(NO INES HEADER)", "NES: not a NES ROM");
    unsigned mapper = (rom[6] >> 4) | (rom[7] & 0xF0);
    chr_bytes = (uint32_t)rom[5] * 0x2000;
    if ((rom[7] & 0x0C) == 0x08 && (rom[9] >> 4) < 0xF)   // NES 2.0: the size's high bits
        chr_bytes += (uint32_t)(rom[9] >> 4) * 256 * 0x2000;
    message("STARTING", nullptr, nullptr);
    // the whole heap from _sbrk now, in one piece: malloc asks _sbrk for 4K
    // blocks beyond what it holds, and the last allocation then doesn't fit
    // a heap that has the room (0.3.x: sdk/cvheap.h)
    cvheap_reserve();
    emu = new Nes_Emu;
    if (!emu) fail("NO MEMORY FOR QUICKNES", nullptr, "NES: no memory for QuickNES");
    cvd_stage(5);
    emu->set_pixels(pic.nes, Nes_Emu::buffer_width);
    emu->set_palette_range(0);
    emu->set_sprite_mode(Nes_Emu::sprites_visible);
    emu->set_scroll_log(scroll_log);
    if (const char *err = emu->load_ines_data(rom, (long)rom_size)) {
        char m1[40], n1[33];
        snprintf(m1, sizeof m1, "MAPPER %u", mapper);
        snprintf(n1, sizeof n1, "NES: mapper %u: %.13s", mapper, err);
        fail(err == unsupported_mapper ? "ITS MAPPER ISN'T SUPPORTED" : "QUICKNES REFUSED IT", m1, n1);
    }
    cvd_stage(6);
    emu->reset(true, true);
    cvd_stage(7);
    mem_left = cvheap_largest();
    snprintf(note, sizeof note, "NES: mapper %u %s %lu", mapper, cache_in_psram ? "psr" : "mem", (unsigned long)mem_left);
    cvf_note(note);

    for (;;) {
        uint32_t before = seen_frames;
        if (!new_frame()) continue;
        if (before && seen_frames - before > 1) late += seen_frames - before - 1;
        game_frame();
        if (!pic_index_staged && pic_index) { pic_index_staged = true; cvd_stage(8); }
        update_note();
    }
}
