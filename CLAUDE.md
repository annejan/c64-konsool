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

The harnesses built for this live in `~/Projects/c64-konsool-harness`, with a
README naming each one. They are kept out of `/tmp`, which a reboot wipes, and
out of the repo, since they are scratch. `kernal_probe.cpp` is the one to
reach for first.

**Test against real disks, not generated ones.** A sync bug hid for hours
because the .d64 generated here contains no byte aligned `$ff` at all, so every
host test passed while every real disk failed. `harness/disks/` holds real
ones.


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

## Config that looks set but is not

Two ways a value in `sdkconfigs/` silently fails to reach the build, both of
which have cost real debugging time:

**A stale `sdkconfig` in the working directory wins.** ESP-IDF only applies
`SDKCONFIG_DEFAULTS` for symbols the existing `sdkconfig` does not already
mention, so a leftover from another branch pins whatever it happens to hold.
It is untracked here and survives a branch switch. Delete it and rebuild
before trusting any config value, and check `build/config/sdkconfig.h` for
what was actually compiled rather than the template.

**An invisible Kconfig symbol cannot be set at all.** A `config` with no
prompt always takes its `default`, whatever the defaults file says, and the
build does not warn. `CONFIG_USB_HOST_EXT_PORT_RESET_ATTEMPTS` is one of
these in IDF v5.5.1: it is `depends on IDF_EXPERIMENTAL_FEATURES`, marked
"Invisible config option", `default 1`. Setting it to 5 does nothing.
Check the symbol in `$IDF_PATH/components/*/Kconfig*` before believing a
config change had any effect.

## A USB drive needs a FATFS volume slot of its own

`CONFIG_FATFS_VOLUME_COUNT` must leave a slot free or a USB drive is turned
away before its filesystem is examined at all. The internal flash (`/int`) and
the SD card take one each, so the count has to be at least 3. The symptom is
that every disk fails identically, whatever it is or how it is formatted:

```
usb_msc: MSC device connected (addr=1)
usb_msc: msc_host_vfs_register failed: ESP_ERR_NOT_FOUND
```

`ESP_ERR_NOT_FOUND` there has one source: `msc_host_vfs_register()` begins
with `ff_diskio_get_drive()`, which reports it when every drive slot is taken.
Reading that error is what identifies the fault; guessing at USB host settings
does not, and there are several plausible-looking ones that are irrelevant.

## Flashing

Over badgelink (USB `16d0:0f9a`), not serial. Two identical
`303a:1001` JTAG devices enumerate and **which one is `ttyACM0` is not
stable**, so tell them apart by what they print: the radio coprocessor says
`Project name: tanmatsu-radio`, the P4 says `transport: Slave chip Id[12]`.
Both consoles are readable while the app runs, so reading a log needs no mode
switch -- but only until the app takes the USB port into host mode for a
gamepad or a drive, at which point the P4 console goes with it and its
`ttyACM` device disappears. Anything that has to be observed while a USB
device is attached needs logging built into the firmware, or has to be shown
on screen; the serial console will not be there. Decode a crash against the matching ELF; running one chip's addresses
through the other's `application.elf` resolves to plausible, entirely wrong
names.

## Formatting

`.clang-format` does not match how much of the existing code is written, so
running it over a whole pre-existing file produces a large diff of unrelated
changes. Format new files freely; for edits to existing ones, keep to the
local style of the surrounding code instead.

## 6502 timing

Codebase64 is the reference for this, at **https://codebase.c64.org/** -- see
`base:6510_instruction_timing`. Note that `codebase64.org` is a DIFFERENT
domain and now redirects to an unrelated site; it is not the wiki any more.

The rules that matter, because ordinary code never notices them and a fast
loader is made of nothing else:

- Absolute indexed and indirect indexed **reads** take one extra cycle when the
  index carries into the high byte. `LDA $nnnn,X` is 4 or 5; `LDA ($nn),Y` is 5
  or 6.
- **Writes** always take the higher count, fixed, because the chip always reads
  the address first: `STA $nnnn,X` is always 5, `STA ($nn),Y` always 6.
- **Read-modify-write** is likewise fixed: `INC $nnnn,X` and `ASL $nnnn,X` are
  always 7. Giving either of these a conditional cycle is a bug.
- Branches are 2 not taken, 3 taken, 4 taken across a page boundary.

Klaus Dormann's suite checks results, not timing, so it passes with every one
of these wrong. Only a fast loader notices, and it shows up as corrupt data
rather than as a crash.

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
