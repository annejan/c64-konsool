# Notes for Claude working in this repo

## Check whether the branch's pull request has already merged before pushing

Work here happens on one long lived branch. Twice in one session commits were
pushed onto that branch *after* its pull request had already merged, so the
work sat unmerged and invisible: not in `main`, not in any open pull request.
Once the merged pull request's description was even edited afterwards, so it
described code the pull request did not contain.

Before pushing, check:

```bash
git fetch origin main
git log --oneline origin/main..HEAD     # what is actually unmerged
```

If the previous pull request has merged, rebase the remaining commits onto the
new `main` and open a *new* pull request. A merged pull request cannot pick up
new commits, and editing its description does not change that.

## Building

The firmware needs ESP-IDF and components from the Espressif component
registry. Where that registry is unreachable, the components can be cloned
from GitHub into `components/` with `IDF_COMPONENT_MANAGER=0`, which is enough
to verify everything compiles. That setup needs local-only edits (pinning
`badge-bsp`, adding `pax-gfx` to `main`'s requires, stubbing a panel driver);
none of them belong in a commit, so check `git status` before staging.

`make build` on a normal machine is still the real check.

## Tests

Three host suites, no ESP-IDF and no hardware needed. Run them before pushing:

```bash
make -C main/src/images/test run          # .t64 and .d64 parsing
make -C main/src/drive/test run           # CBM DOS, GCR, the 1541 hardware
make -C main/src/test run                 # the 6502 itself
make -C components/hidhost/test_native test   # what a gamepad becomes as a joystick
```

They also run in CI on every pull request.

## Driving the emulator from the host

Anything that goes wrong between the C64 and the drive can be reproduced on a
normal machine, which is far faster than reflashing to try an idea. Both sides
are plain C++ with no ESP-IDF in them, so a small harness can compile
`CPU6502`, `Drive1541`, `DiskController`, `Gcr`, `Via6522` and `D64Disk`
natively, put the kernal from `main/src/roms/kernal.h` on one side and a real
1541 DOS ROM on the other, and interleave the two the way `CPUC64` does:

```cpp
uint8_t before = c64.numofcycles;
c64.step();
unsigned spent = c64.numofcycles - before;
driveDebt += spent;      // the drive earns a cycle for every cycle the C64 spends
drive.countTimers(spent) // stepInstruction() does NOT advance the VIA timers
```

Two things that are easy to get wrong in such a harness and cost an evening
each: `stepInstruction()` runs the drive CPU but leaves its VIA timers alone,
so `countTimers()` has to be called separately or every timed handshake fails;
and the kernal times the bus with CIA 1 timer B (`$DC06`/`$DC07`/`$DC0F`,
underflow read back from `$DC0D`), so a stub C64 without it hangs in the EOI
turnaround rather than in anything real.

Ask the drive what it thinks rather than inferring from symptoms. Channel 15
is always open and reports the DOS's own view (`73,CBM DOS V2.6 1541,00,00`),
and the job queue in drive RAM is even more direct: `$00`-`$05` are the job
codes per buffer, `$06`-`$11` the track and sector each wants, and the code is
replaced by the result -- `$01` ok, `$02` header not found, `$03` no sync,
`$04` data block not found, `$05` data checksum, `$0b` id mismatch.

## Flashing

Over badgelink (USB `16d0:0f9a`), not serial. Two identical
`303a:1001` JTAG devices enumerate and **which one is `ttyACM0` is not
stable**, so tell them apart by what they print: the radio coprocessor says
`Project name: tanmatsu-radio`, the P4 says `transport: Slave chip Id[12]`.
Both consoles are readable while the app runs, so reading a log needs no mode
switch. Decode a crash against the matching ELF; running one chip's addresses
through the other's `application.elf` resolves to plausible, entirely wrong
names.

## Formatting

`.clang-format` does not match how much of the existing code is written, so
running it over a whole pre-existing file produces a large diff of unrelated
changes. Format new files freely; for edits to existing ones, keep to the
local style of the surrounding code instead.

## Emulator references

The drive emulation follows [Frodo](https://github.com/cebix/frodo4), and the
disk format details come from [VICE](https://vice-emu.sourceforge.io/). Both
are GPL v2 or later, compatible with this project's GPL v3. Where behaviour is
taken from either, the copyright notice travels with it. Prefer checking one of
them over reasoning from first principles: the polarity of the serial lines
differs between the two ends of the bus, and the status byte values are not
guessable.

The 1541 DOS ROM is copyrighted and deliberately not in this repo. It is read
from `/c64prg/1541.rom` on the SD card at runtime. A VICE install has a usable
copy in `/usr/share/vice/DRIVES/`: `dos1541-325302-01+901229-05.bin` is the
original two part ROM concatenated, which is the flat 16K the drive maps at
`$C000`.

Where a VICE checkout is to hand, `src/drive/rotation.c` is the reference for
how the head is meant to behave. It models rotation at the bit level with an
accumulator and treats sync as ten consecutive one bits, where this project is
byte granular; and it keeps `BRA_MOTOR_ON` and `BRA_BYTE_READY` apart, the
first turning the disk and the second only gating the signal to the CPU.
