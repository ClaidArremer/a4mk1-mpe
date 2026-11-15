/* Host test for mpe.c: the stock addresses are redirected to a fake memory. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#define HOST
static uint8_t mem[0x10000]; static uint8_t sram[0x10000];
static void *fake(uint32_t a) {
    if (a >= 0x80000000u && a < 0x80010000u) return &sram[a - 0x80000000u];
    return &mem[a & 0xffff];
}
#define V8(a)  (*(volatile uint8_t *)fake(a))
#define V32(a) (*(volatile uint32_t *)fake(a))
#define KIT_INDEX 0x1000u
#define ACTIVE_TRACK 0x1004u
#define KIT_STRIDE 0x10u
#define KIT_RX_NOTES 0x1008u
#define POLY_VOICES 0x80003d28u
#define SRC_BREATH 0x80005f8cu
#define SRC_MODWHEEL 0x80005f9au
#define SRC_AFTERTOUCH 0x80005fb6u
#define VSTATE 0x2000u
static unsigned char statebuf[512] __attribute__((aligned(4)));
#define STATE ((uintptr_t)statebuf)
#define SRC16(base, i) (((volatile short *)fake(base))[i])
#define SEC(x)
#define VSP(a) ((volatile unsigned int *)fake(a))
#define POLY_FLAG 0x80003d30u
#define NOTE_TABLE 0x3000u
#define TRACK_INUSE 0x2f00u
typedef void (*midi_h)(const unsigned char *, unsigned int, unsigned int);
static int calls_stock, last_on_track = -1, last_on_note = -1, last_off_note = -1, released = -1;
static void s_h(const unsigned char *m, unsigned int a, unsigned int b) { (void)m; (void)a; (void)b; calls_stock++; }
static void t_on(unsigned int t, unsigned int n, unsigned int v, unsigned int p, unsigned int z, int m1) { (void)v;(void)p;(void)z;(void)m1; last_on_track = t; last_on_note = n; }
static void t_off(unsigned int t, unsigned int n, unsigned int p) { (void)t;(void)p; last_off_note = n; }
static unsigned int p_alloc(unsigned int t, unsigned int n, unsigned int *o, unsigned int m, unsigned int s) { (void)t;(void)n;(void)o;(void)m;(void)s; return 0xdead; }
static void v_rel(unsigned int v) { released = v; }
#define STOCK_NOTE_OFF s_h
#define STOCK_NOTE_ON s_h
#define STOCK_CC s_h
#define STOCK_PRESSURE s_h
#define STOCK_BEND s_h
#define TRACK_NOTE_ON t_on
#define TRACK_NOTE_OFF t_off
#define POLY_ALLOC p_alloc
#define VOICE_RELEASE v_rel
#include "mpe.c"
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
static unsigned char M[3];
static void msg(int st, int a, int b) { M[0] = st; M[1] = a; M[2] = b; }
int main(void) {
    memset(statebuf, 0xa5, sizeof statebuf);
    V32(KIT_INDEX) = 0; V32(ACTIVE_TRACK) = 0; V32(KIT_RX_NOTES) = 1;
    /* poly off: everything goes to stock */
    V32(POLY_VOICES) = 0; calls_stock = 0;
    msg(0x91, 60, 100); mpe_note_on(M, 0, 0); CHECK(calls_stock == 1);
    /* poly on, voices 1-4 */
    V32(POLY_VOICES) = 0xf; calls_stock = 0;
    msg(0x92, 64, 100); mpe_note_on(M, 0, 0x10);
    CHECK(calls_stock == 0 && last_on_track == 0 && last_on_note == 64 && S->note_voice[64] == 1);
    msg(0x90, 48, 100); mpe_note_on(M, 0, 0); CHECK(calls_stock == 1);            /* manager note: stock */
    msg(0x95, 50, 100); mpe_note_on(M, 0, 0); CHECK(calls_stock == 2);            /* ch6 outside zone */
    unsigned int out = 7, mask = mpe_alloc(0, 64, &out, 0xf, 0);
    CHECK(mask == 2 && released == 1 && S->note_voice[64] == -1);
    CHECK(V8(NOTE_TABLE + 64 * 3) == 2 && V8(TRACK_INUSE) == 2);
    CHECK(((volatile uint32_t *)fake(VSTATE + 16))[2] == 64 && ((volatile uint32_t *)fake(VSTATE + 16))[1] == 1);
    CHECK(mpe_alloc(0, 65, &out, 0xf, 0) == 0xdead);                          /* not forced: stock */
    msg(0x82, 64, 0); mpe_note_off(M, 0, 0); CHECK(last_off_note == 64);
    msg(0x93, 70, 0); mpe_note_on(M, 0, 0); CHECK(last_off_note == 70);         /* vel 0 = off */
    /* bend: member full up = 0x7fff, manager adds /24 */
    msg(0xE2, 0x7f, 0x7f); mpe_bend(M, 0, 0); CHECK(S->mem_bend[1] == 0x7fff);
    msg(0xE3, 0x00, 0x40); mpe_bend(M, 0, 0); CHECK(S->mem_bend[2] == 0x4000);
    msg(0xE3, 0x00, 0x00); mpe_bend(M, 0, 0); CHECK(S->mem_bend[2] == 0);
    msg(0xE0, 0x7f, 0x7f); mpe_bend(M, 0, 0);
    /* slide/pressure */
    msg(0xB3, 74, 127); mpe_cc(M, 0, 0); CHECK(S->mem_slide[2] == 0x7fff);
    msg(0xD4, 64, 0); mpe_pressure(M, 0, 0); CHECK(S->mem_press[3] > 0x3f00 && S->mem_press[3] < 0x4100);
    calls_stock = 0; msg(0xB0, 7, 100); mpe_cc(M, 0, 0); CHECK(calls_stock == 1);  /* other manager CC */
    msg(0xB2, 7, 100); mpe_cc(M, 0, 0); CHECK(calls_stock == 1);                   /* member CC dropped */
    /* engine index: voice 2 reads our block */
    ((volatile short *)fake(SRC_AFTERTOUCH))[0] = 100;
    unsigned int idx = mpe_voice_index(0, 2);
    short *base = (short *)fake(SRC_BREATH);
    (void)base; (void)idx;
    CHECK(S->block[14 + 2] == (short)(0 + (0x7fff - 0x4000) / 24));             /* member 0 + manager full */
    CHECK(S->block[7 + 2] == 0x7fff && S->block[21 + 2] == 100);
    CHECK(mpe_voice_index(0, 4) == 0);                                        /* FX/CV untouched */
    /* MCM: RPN 6 = 0 turns MPE off */
    msg(0xB0, 101, 0); mpe_cc(M, 0, 0); msg(0xB0, 100, 6); mpe_cc(M, 0, 0); msg(0xB0, 6, 0); mpe_cc(M, 0, 0);
    calls_stock = 0; msg(0x92, 60, 100); mpe_note_on(M, 0, 0); CHECK(calls_stock == 1);
    msg(0xB0, 6, 2); mpe_cc(M, 0, 0); calls_stock = 0;
    msg(0x92, 60, 100); mpe_note_on(M, 0, 0); CHECK(calls_stock == 0);
    msg(0x93, 60, 100); mpe_note_on(M, 0, 0); CHECK(calls_stock == 1);              /* only 2 members now */
    /* active track not a poly voice: stock */
    msg(0xB0, 6, 4); mpe_cc(M, 0, 0); V32(POLY_VOICES) = 0xe; calls_stock = 0;
    msg(0x92, 60, 100); mpe_note_on(M, 0, 0); CHECK(calls_stock == 1);
    printf("%s (%d failures)\n", fails ? "FAILED" : "all passed", fails);
    return fails != 0;
}
