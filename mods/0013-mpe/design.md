# 0013 MPE - how it works

All addresses OS 1.56, found by static analysis of the stock image (capstone listing,
`tools/disasm.sh` equivalent). Status: built, host-tested (`test_host.c`), **not yet
run on hardware**.

## MIDI in

The MIDI task at 0x4006385a takes each message off its queues and calls one of 16
handler pointers at **0x4017b598**, indexed by status >> 4, as `h(msg, a2, a3)`:

| status | stock handler | what it does |
|---|---|---|
| 0x80 | 0x40064ede | note off -> 0x40064e0e(ch, note, a2) -> per track 0x40064c4e(track, note, a2) |
| 0x90 | 0x400652ae | note on -> 0x400651b8(ch, note, vel, a3) -> per track 0x40064f1c(track, note, vel, a3, 0, -1) |
| 0xA0 | 0x4006369e | `rts` - stock ignores poly aftertouch |
| 0xB0 | 0x40063cee | CC -> per track 0x40066336 |
| 0xD0 | 0x40063e48 | channel pressure (data << 8) -> 0x400fa882(track, v) |
| 0xE0 | 0x40063f54 | bend ((lsb + msb << 7) * 2) -> 0x400fa8fa(track, v) |

Tracks 0..5 (synth 1-4, FX, CV) match a message by their MIDI channel (kit
0x40958f68 + kit * 0x26c + 0x315ab0) or, on the auto channel (0x415afa24 + kit *
0x9b0), by being the active track (0x415af866). Receive notes: 0x415afa78 + kit * 0x9b0.

0013 replaces entries 8, 9, B, D, E. Each handler falls straight through to the stock
one unless MPE is live and the channel is in the zone.

## Modulation sources

Per track (index 0..5, 6 = poly/keyboard) the CPU keeps four s16 sources in SRAM, each
0..0x7fff: breath 0x80005f8c, mod wheel 0x80005f9a, bend 0x80005fa8 (centre 0x4000),
aftertouch 0x80005fb6 (7 x 2 B apart). The control-rate engine loop at
0x400fd162..0x400fd1d0 runs once per voice k = 0..5: s = 0x80005548[k] (the track that
started the voice's note, written at 0x400fc8b4), then velocity[k] (0x425d5b28, already
per voice) and bend[s], wheel[s], breath[s], aftertouch[s] go through 0x400fa20a into the
sound's five destinations per source (SOUND SETUP's menus).

In poly mode every voice's note comes from the poly track, so s is the same for all
voices and bend / wheel / aftertouch are shared. `mpe_loop_hook` (patched in at
0x400fd164) swaps s for an index into the MPE block, which has the same layout as the
stock arrays, so the loop's single index register reaches all four. Velocity is left
alone (already per voice).

Scalings are the stock ones: bend `((v - 0x4000) * 0x10410 >> 16) + 0x4000` (0x400fa8fa),
pressure / CC `(v7 << 8) * 0x8102 >> 15` (0x400fa882, 0x400fa972). Full PITCH BEND depth
on OSC pitch was measured on hardware at 48 semitones, which is MPE's default member
bend range, so member bend needs no rescaling. The manager's (+-2 st) is divided by 24.

## Voice allocation

Note on -> 0x40064f1c builds a note event and queues it (0x400ff6ec). The engine's note
start (loop at 0x400fc3fa) calls, for a poly track, the allocator **0x400ff284**(track,
note, &out, voice mask, sub) at 0x400fc4a4; non-poly tracks use 0x400ff152. It returns
the voice bit mask. Allocator state: 0x415c0ab4 + 16 * voice {age, active, note, track},
the per-track note table 0x415c0b14 + track * 0x180 + note * 3 + sub (voice bits),
0x415c0aac[track] (voices in use), mode 0x80003d2c (rotate / reassign / reset), poly
voice mask 0x80003d28 (copied from the kit's POLY CONFIG at kit + 0x934 by 0x400fa6e8).
0x400ff200(voice) releases a voice before reuse.

`mpe_note_on` records note -> member index (state `note_voice`) and plays the note on
the active track; `mpe_alloc` (the new call target) takes that voice instead of
asking the allocator, with the same bookkeeping as the allocator's own
voice-not-in-poly branch (age all voices, release the voice, set its note-table and
in-use bits, fill its state), so note off, stealing and the stock modes are unchanged.

## State

Uncached free DDR at 0x4680e000 (struct mpe, < 0x100 B), which holds garbage at boot:
`st()` initialises it behind a magic word on first use.

## Known limits

- Two member channels holding the same note number share one note-table entry; a note
  off on either releases both (stock poly behaves the same for repeated notes).
- "Live" is the poly voice mask containing the active track. If that mask can be
  non-zero with poly switched off, member notes still go to the active track and play
  mono there (no voice forcing: the allocator is only called in poly).
- Member-channel CCs other than 74 are dropped.
