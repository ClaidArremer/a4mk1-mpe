# a4mk1-mpe — MPE for the Elektron Analog Four MK1

**For the Analog Four MK1 only, OS 1.56.** Not for the Analog Four MKII, Analog Keys or
any other Elektron unit. Tested on hardware with a ROLI Seaboard RISE.

A firmware mod that adds MIDI Polyphonic Expression (MPE) to the Analog Four MK1, as an
extension of its poly mode. It is a mod for
[hugz202's AE-analog-four-mods](https://github.com/hugz202/AE-analog-four-mods) build
framework (mod id `0013-mpe`): you build your own patched OS from your own copy of the
stock OS with that framework. No Elektron OS files are included here.

## What it does

- **MPE Lower Zone:** channel 1 is the manager (global), channels 2–5 are the members,
  one per poly voice (voices 1–4). Member pitch bend range ±48 semitones; manager
  bend ±2.
- Every note gets its own expression, routed through the stock SOUND SETUP mod menus
  like any other modulation source:
  - **Strike** (note-on velocity) → the sound's VELOCITY modulation
  - **Bend** (per-note pitch bend) → its PITCH BEND modulation
  - **Slide** (CC74) → its MOD WHEEL modulation
  - **Pressure** (channel pressure) → its AFTERTOUCH modulation
- The manager channel's bend, pressure, CC74 and CC1 apply to every voice; its other
  messages behave as stock.
- An MPE Configuration Message (RPN 6 on channel 1) sets the number of member channels;
  0 turns MPE off until the next one.
- Live only while poly mode is on and the active track is one of the poly voices;
  otherwise everything is stock. The stock "use track sounds" poly option still works.
- Optional per-kit **MPE on/off** switch (saved with the kit, **off by default**), when
  built with `PARAMS="0013-mpe:kit_switch=1"` together with
  [a4mk1-arplocks](https://github.com/ClaidArremer/a4mk1-arplocks), which puts the switch
  on the ARP page (knob J). Built without it (`kit_switch = 0`, the default), MPE is
  always available.

## A4 setup

- GLOBAL > MIDI CONFIG: performance channel 1; tracks 1–4 on channels 2–5.
- Poly mode on for the kit, voices 1–4.
- Controller in MPE mode, Lower Zone, 4 member channels, bend range 48.

## Building

1. Get [AE-analog-four-mods](https://github.com/hugz202/AE-analog-four-mods) and follow
   its BUILD.md (you supply the stock OS 1.56 yourself).
2. Copy `mods/0013-mpe` into its `mods/` folder.
3. Append `registry/0013-mpe.toml` to its `registry/allocations.toml`.
4. Build, e.g.:

   ```
   make custom MODS="0001-scale-lock 0002-slide-303 0006-fx-slot 0011-rand-track 0013-mpe" TAG=mpe
   ```

   With a4mk1-arplocks (MPE switch on the ARP page, off by default), see that repo's
   README for the full command; it adds `PARAMS="0013-mpe:kit_switch=1"`.

   0013 cannot share an image with `0010-wave-osc` or `0012-res-limit` (it uses their
   code space).

`mods/0013-mpe/mpe.c` and `hook.s` are the source; `gen.sh` regenerates `stub.s` (needs
an m68k gcc). `test_host.c` runs the logic on a computer. `design.md` explains how the
stock addresses were found.

## Status

Hardware-verified on an Analog Four MK1, OS 1.56. Use at your own risk: back up your
projects before flashing a modded OS. The bootloader is never touched, so you can always
reflash the stock OS.

## Credits

Built on hugz202 / Artificial Energy's AE-analog-four-mods framework (MIT).
MIT licence.
