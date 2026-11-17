// nesapu.h -- QuickNES's APU state as nescv_apu_t (C++, header only; the
// plugin and the Mac test include it after nes_emu.h).
#pragma once
#include "apu_state.h"
#include "nes_apu.h"
extern "C" {
#include "nescv.h"
}

static inline void nesapu_read(const Nes_Apu &apu, nescv_apu_t *a)
{
    apu_state_t st;
    apu.save_state(&st);
    const uint8_t *r = st.apu.w40xx;
    const apu_state_t::square_t *sq[2] = {&st.square1, &st.square2};
    for (int k = 0; k < 2; k++) {
        const uint8_t *q = r + 4 * k;
        int period = (q[3] & 7) << 8 | q[2];
        // Nes_Square::run's silence: no length, period < 8, or a sweep target past $7FF
        int offset = (q[1] & 0x08) ? 0 : period >> (q[1] & 7);
        int vol = (q[0] & 0x10) ? (q[0] & 15) : sq[k]->env[1];
        if (sq[k]->length_counter == 0 || period < 8 || period + offset >= 0x800)
            vol = 0;
        a->pulse_period[k] = (uint16_t)period;
        a->pulse_vol[k] = (uint8_t)vol;
    }
    a->tri_period = (uint16_t)((r[11] & 7) << 8 | r[10]);
    a->tri_on = st.triangle.length_counter && st.triangle.linear_counter && a->tri_period >= 2;
    int nvol = (r[12] & 0x10) ? (r[12] & 15) : st.noise.env[1];
    a->noise_vol = (uint8_t)(st.noise.length_counter ? nvol : 0);
    a->noise_rate = r[14] & 15;
}
