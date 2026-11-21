# Star Trek (1971) for KIM-1

A 6502 port of the original *Star Trek* game, based on the ANSI C port in
[`startrek1971`](../startrek1971). The game is written in C for cc65; the small
assembly module calls the KIM-1 monitor's serial input and output routines.

## Build and ROM layout

The target is a KIM-1 with 32 KB of expansion RAM at `$2000-$9FFF` and a ROM
mapped at `$A000`. From this directory, run:

```sh
make
```

This creates `star-trek.bin`, a raw binary linked to start at `$A000` (no
load-address header). The cc65 runtime and game code are in ROM. Global data
uses expansion RAM beginning at `$2000`, and the cc65 software stack uses the
top 2 KB of expansion RAM. The linker map is written to `star-trek.map`.

Flash the binary at `$A000`, then start it from the KIM-1 monitor with `G A000`.
The image does not provide a hardware reset vector.

The serial routines call the KIM-1 monitor `INCH` (`$1E5A`) and `OUTCH`
(`$1EA0`); play through the monitor's serial terminal. Numeric input is entered
one line at a time.

## Commands

The port retains the original command numbers:

| Command | Action |
| --- | --- |
| 0 | Warp movement |
| 1 | Short-range sector scan |
| 2 | Long-range 3x3 quadrant scan |
| 3 | Fire phasers |
| 4 | Fire photon torpedoes |
| 5 | Transfer energy to shields |
| 6 | Damage report |
| 7 | Library computer: galaxy record, status, or torpedo data |

Courses are integer directions: `1=E`, `2=NE`, `3=N`, `4=NW`, `5=W`,
`6=SW`, `7=S`, `8=SE`. Unlike the C original, this version does not accept
fractional courses. It uses integer arithmetic and KIM-1 monitor I/O rather
than hosted `stdio` or floating point.

The source is derived from the LGPL-3.0-or-later ANSI C port by busfahrer, based
on Michael Birken's C# port of Mike Mayfield's game.
