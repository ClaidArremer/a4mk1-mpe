/* 0013-mpe: MPE Lower Zone for the Analog Four / Analog Keys MK1, OS 1.56.
 *
 * Manager channel 1, member channels 2-5 -> poly voices 1-4 (one note per channel).
 * Per voice: Strike = note velocity (stock VELOCITY source), Bend = PITCH BEND source,
 * Slide (CC74) = MOD WHEEL source, Pressure (channel pressure) = AFTERTOUCH source;
 * routed with the stock SOUND SETUP modulation menus. Manager bend (+-2 st),
 * pressure, CC74 and CC1 apply to every voice. RPN 6 on channel 1 (MCM) sets the
 * member count; 0 turns MPE off.
 * Live only while the active track is one of the poly voices; otherwise, and for
 * anything outside the zone, the stock handlers run unchanged.
 *
 * Source of truth for stub.s (gen.sh compiles it). Integer only, no FPU.
 * Runs in the MIDI task (handlers) and the control-rate engine (voice index, alloc).
 */
typedef unsigned char u8;
typedef signed char s8;
typedef signed short s16;
typedef unsigned int u32;
typedef signed int s32;

#ifndef HOST
#define V8(a)  (*(volatile u8 *)(a))
#define V32(a) (*(volatile u32 *)(a))
#define SRC16(base, i) (((volatile s16 *)(base))[i])
#define VSP(a) ((volatile u32 *)(a))
#define SEC(s) __attribute__((section(s)))

/* ---- stock OS 1.56 (see re/symbols.toml notes in mod.toml) ---- */
#define KIT_INDEX      0x415af896u       /* current kit index */
#define ACTIVE_TRACK   0x415af866u       /* active track 0..5 */
#define KIT_STRIDE     0x9b0u
#define KIT_RX_NOTES   0x415afa7au       /* + kit*0x9b0: receive notes on */
#define POLY_VOICES    0x80003d28u       /* engine copy of the poly voice mask */
#define POLY_FLAG      0x80003d30u       /* allocator's out-flag source */
#define SRC_BREATH     0x80005f8cu       /* 7 x s16 per source, index = track */
#define SRC_MODWHEEL   0x80005f9au
#define SRC_AFTERTOUCH 0x80005fb6u
#define VSTATE         0x415c0ab4u       /* 16 B per voice {age, active, note, track} */
#define NOTE_TABLE     0x415c0b14u       /* + track*0x180 + note*3 + sub: voice bits */
#define TRACK_INUSE    0x415c0aacu       /* u8 per track: voice bits */
#define STATE          0x4680e000u       /* free DDR (0x46803000..0x4680ffff gap) */

typedef void (*midi_h)(const u8 *msg, u32 a2, u32 a3);
#define STOCK_NOTE_OFF ((midi_h)0x40064edeu)
#define STOCK_NOTE_ON  ((midi_h)0x400652aeu)
#define STOCK_CC       ((midi_h)0x40063ceeu)
#define STOCK_PRESSURE ((midi_h)0x40063e48u)
#define STOCK_BEND     ((midi_h)0x40063f54u)
#define TRACK_NOTE_ON  ((void (*)(u32, u32, u32, u32, u32, s32))0x40064f1cu)
/* note off: stock passes the handler's third argument through (0x40064ede ->
 * 0x40064e0e(ch, note, a3) -> 0x40064c4e(track, note, a3)) */
#define TRACK_NOTE_OFF ((void (*)(u32, u32, u32))0x40064c4eu)
#define POLY_ALLOC     ((u32 (*)(u32, u32, u32 *, u32, u32))0x400ff284u)
#define VOICE_RELEASE  ((void (*)(u32))0x400ff200u)
#endif

#define NV 4
#define MAGIC 0x4d504531u                /* "MPE1" */

/* All state in uncached free DDR, which holds garbage at boot: init on first use. */
struct mpe {
    u32 magic;
    u8 members, rpn_msb, rpn_lsb, pad;
    s16 mem_bend[NV], mem_slide[NV], mem_press[NV];
    s16 mgr_bend, mgr_slide, mgr_wheel, mgr_press;
    s8 note_voice[128];
    /* read by the engine's modulation loop in place of the per-track arrays; same
     * spacing as stock (breath, mod wheel, bend, aftertouch, 7 x s16 each), so the
     * loop's one index register reaches all four */
    s16 block[4 * 7];
};
#define S ((struct mpe *)STATE)

SEC(".mpe1") static struct mpe *st(void)
{
    struct mpe *s = S;
    if (s->magic != MAGIC) {
        u32 i, *nv = (u32 *)s->note_voice;
        s->members = NV; s->rpn_msb = s->rpn_lsb = 0x7f;
        for (i = 0; i < NV; i++) { s->mem_bend[i] = 0x4000; s->mem_slide[i] = s->mem_press[i] = 0; }
        s->mgr_bend = 0x4000; s->mgr_slide = s->mgr_wheel = s->mgr_press = 0;
        for (i = 0; i < 32; i++) nv[i] = 0xffffffffu;
        s->magic = MAGIC;
    }
    return s;
}

SEC(".mpe1") static s32 clamp15(s32 v) { return v < 0 ? 0 : (v > 0x7fff ? 0x7fff : v); }

/* The kit's MPE switch (0015's ARP page, knob J): byte 52 of the FX track's spare
 * storage (0006 saves it with the kit), nonzero = MPE on, so kits start with MPE off.
 * Only with the build parameter kit_switch = 1 (a build with 0015); otherwise MPE is
 * always available. */
#define PROJECT_PTR    0x415cba28u
#define KIT_FX_STORE   4404             /* project + this: the FX track's storage */
#define MPE_ON_BYTE    52

/* MPE applies while enabled and the active track is a poly voice. */
SEC(".mpe9") static int live(struct mpe *s)
{
    u32 ks;
    __asm__ ("move.l #P_KIT_SWITCH,%0" : "=d" (ks));    /* the build parameter */
    if (ks) {
        u32 p = V32(PROJECT_PTR), k = p ? V32(p + KIT_FX_STORE) : 0;
        if (!k || !V8(k + MPE_ON_BYTE))
            return 0;
    }
    u32 t = V32(ACTIVE_TRACK);
    return s->members && t < NV && ((V32(POLY_VOICES) >> t) & 1);
}

/* zone member index 0..3 for a channel, -1 = manager, -2 = outside the zone */
SEC(".mpe1") static int member(struct mpe *s, u32 ch)
{
    if (ch == 0) return -1;
    if (ch <= s->members && ch <= NV) return (int)ch - 1;
    return -2;
}

SEC(".mpe2") void mpe_note_on(const u8 *m, u32 a2, u32 a3)
{
    struct mpe *s = st();
    int v;
    if (!live(s) || (v = member(s, m[0] & 15)) < 0) { STOCK_NOTE_ON(m, a2, a3); return; }
    if (!V32(KIT_RX_NOTES + V32(KIT_INDEX) * KIT_STRIDE)) return;
    if (m[2] == 0) { TRACK_NOTE_OFF(V32(ACTIVE_TRACK), m[1] & 127, a3); return; }
    s->note_voice[m[1] & 127] = (s8)v;
    TRACK_NOTE_ON(V32(ACTIVE_TRACK), m[1] & 127, m[2], a3, 0, -1);
}

SEC(".mpe3") void mpe_note_off(const u8 *m, u32 a2, u32 a3)
{
    struct mpe *s = st();
    if (!live(s) || member(s, m[0] & 15) < 0) { STOCK_NOTE_OFF(m, a2, a3); return; }
    if (!V32(KIT_RX_NOTES + V32(KIT_INDEX) * KIT_STRIDE)) return;
    TRACK_NOTE_OFF(V32(ACTIVE_TRACK), m[1] & 127, a3);
}

/* stock scaling of 0x400fa8fa: 14-bit bend -> 0..0x7fff, centre 0x4000 */
SEC(".mpe4") void mpe_bend(const u8 *m, u32 a2, u32 a3)
{
    struct mpe *s = st();
    int v = member(s, m[0] & 15);
    s32 b;
    if (!live(s) || v == -2) { STOCK_BEND(m, a2, a3); return; }
    b = (s32)((((u32)(m[1] & 127) | ((u32)(m[2] & 127) << 7)) << 1)) - 0x4000;
    b = clamp15(((b * 0x10410) >> 16) + 0x4000);
    if (v < 0) s->mgr_bend = (s16)b; else s->mem_bend[v] = (s16)b;
}

/* stock scaling of 0x400fa882 / 0x400fa972: 7-bit -> 0..0x7fff */
SEC(".mpe1") __attribute__((noinline)) static s16 uni7(u32 v7) { return (s16)clamp15((s32)((v7 << 8) * 0x8102) >> 15); }

SEC(".mpe5") void mpe_pressure(const u8 *m, u32 a2, u32 a3)
{
    struct mpe *s = st();
    int v = member(s, m[0] & 15);
    if (!live(s) || v == -2) { STOCK_PRESSURE(m, a2, a3); return; }
    if (v < 0) s->mgr_press = uni7(m[1] & 127); else s->mem_press[v] = uni7(m[1] & 127);
}

/* MPE Configuration Message on the manager channel: RPN 6, data = member count */
SEC(".mpe8") __attribute__((noinline)) static void mcm(struct mpe *s, u32 cc, u32 val)
{
    if (cc == 101) s->rpn_msb = (u8)val;
    else if (cc == 100) s->rpn_lsb = (u8)val;
    else if (cc == 6 && s->rpn_msb == 0 && s->rpn_lsb == 6) s->members = (u8)val;
}

SEC(".mpe6") void mpe_cc(const u8 *m, u32 a2, u32 a3)
{
    struct mpe *s = st();
    u32 ch = m[0] & 15, cc = m[1] & 127;
    int v;
    s16 x;
    if (ch == 0) mcm(s, cc, m[2] & 127);
    v = member(s, ch);
    if (!live(s) || v == -2) { STOCK_CC(m, a2, a3); return; }
    x = uni7(m[2] & 127);
    if (v >= 0) {                                   /* member: only Slide is used */
        if (cc == 74) s->mem_slide[v] = x;
        return;
    }
    if (cc == 74) s->mgr_slide = x;
    else if (cc == 1) s->mgr_wheel = x;
    else STOCK_CC(m, a2, a3);                       /* everything else as stock */
}

/* Engine modulation loop, voice k (0..5), src = the stock source track for it.
 * Returns the index the loop then uses for bend/mod wheel/breath/aftertouch. */
SEC(".mpe1") u32 mpe_voice_index(u32 src, u32 k)
{
    struct mpe *s = st();
    s32 b, w, p;
    if (k >= NV || src > 6 || !live(s)) return src;
    s->block[k] = SRC16(SRC_BREATH, src);
    b = (s32)s->mem_bend[k] + ((s32)s->mgr_bend - 0x4000) / 24;  /* +-2 st vs +-48 */
    s->block[14 + k] = (s16)clamp15(b);
    w = s->mem_slide[k];
    if (s->mgr_slide > w) w = s->mgr_slide;
    if (s->mgr_wheel > w) w = s->mgr_wheel;
    if (SRC16(SRC_MODWHEEL, src) > w) w = SRC16(SRC_MODWHEEL, src);
    s->block[7 + k] = (s16)w;
    p = s->mem_press[k];
    if (s->mgr_press > p) p = s->mgr_press;
    if (SRC16(SRC_AFTERTOUCH, src) > p) p = SRC16(SRC_AFTERTOUCH, src);
    s->block[21 + k] = (s16)p;
    return (u32)(((s32)(u32)&s->block[0] - (s32)SRC_BREATH) / 2) + k;
}

/* Poly allocator wrapper: a note from member channel c plays on voice c-2, with the
 * stock allocator's bookkeeping (0x400ff284, the voice-not-in-poly branch). */
SEC(".mpe7") u32 mpe_alloc(u32 track, u32 note, u32 *out, u32 mask, u32 sub)
{
    struct mpe *s = st();
    volatile u32 *vs;
    u32 i;
    int v;
    if (note > 127 || (v = s->note_voice[note]) < 0 || !((mask >> track) & 1) || !((mask >> v) & 1))
        return POLY_ALLOC(track, note, out, mask, sub);
    s->note_voice[note] = -1;
    for (i = 0; i < NV; i++) {                      /* age every voice, as stock does */
        vs = VSP(VSTATE + 16 * i);
        if (vs[0] != 0x7fffffffu) vs[0]++;
        if (vs[1] - 1u <= 0x7ffffffdu) vs[1]++;
    }
    VOICE_RELEASE((u32)v);
    V8(NOTE_TABLE + track * 0x180 + note * 3 + sub) |= (u8)(1 << v);
    V8(TRACK_INUSE + track) |= (u8)(1 << v);
    vs = VSP(VSTATE + 16 * (u32)v);
    vs[0] = 0; vs[1] = 1; vs[2] = note; vs[3] = track;
    *out = V32(POLY_FLAG) != 0;
    return 1u << v;
}
