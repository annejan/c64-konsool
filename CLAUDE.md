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

## The host build, and why it has to be lock stepped

`host/` builds the whole emulator for a normal machine. It is the only way to
check a change without a badge, and the gate it provides is "these demos render
byte identically before and after":

```bash
make -C host -j8
./host/c64host --frames 400 --prg demo.prg --autostart --screenshot out.ppm
./host/c64host --frames 900 --truedrive --drive-rom 1541.rom \
    --disk side1.d64 --type $'LOAD"*",8,1\rRUN\r' --screenshot out.ppm
```

That gate is worth nothing unless the runner is deterministic, and it very
nearly was not. The badge runs the emulation on one core and the display on the
other; the host keeps that shape, and the first version paced the two with a
sleep. The display thread then read the VIC's bitmap while the emulation was
still writing it, so **two runs of the same binary produced different
screenshots and different RAM**. That is not a flaky test, it is a broken
instrument: it reported a regression in kloten that did not exist, and a
correct bus fix was reverted because of a difference that turned out to be
noise.

It is fixed by waiting on the other thread's state rather than on a clock:
`hostWaitUntilParked()` blocks until the emulation is parked in
`xSemaphoreTake` at the end of its frame, and only then is the framebuffer
read. Exactly one emulated frame per drawn frame, no sleeps anywhere. **Do not
reintroduce a sleep here.** Before trusting any before/after comparison, run
the same binary twice and check it agrees with itself.

Disk runs are covered too: kloten loaded over the emulated 1541 comes back
identical in both the screenshot and all 64K.

## What the host build cannot check

It links no pax, so nothing in `main/src/menuoverlay/` or the boot screen is
verifiable here, and there are no tests for that layer either. Every menu
change is unverified until someone looks at the badge. Two PETSCII sizing
mistakes in one evening were caught that way and no other.

## The serial bus, and the polarities already checked

Frodo is the reference (`CLAUDE.md` says so above, and it holds). These four
were each checked against it and are right, so there is no need to go round
them again:

- **C64 output**: a line is released only by driving its bit with a zero. A one
  pulls it low, and so does leaving the bit an input, because the pin floats
  high and the inverting driver turns that into a pulled down line. Frodo:
  `inv_out = ~pra & ddra` (`src/CIA.cpp`, `write_pa`). A loader can therefore
  bit bang the bus from `$dd02` alone, and The Lab does.
- **Drive output**: DATA from VIA1 PB1, CLK from PB3, and the drive never
  drives ATN (`src/CPU1541.cpp`, `set_iec_lines`).
- **ATN acknowledge**: DATA is pulled low when the ATN line state equals the
  ATNA bit (`CalcIECLines`). Ours reads `dataOut || (atna != atnLow())`, which
  is the same thing.
- **Drive input**: ATN arrives on VIA1 PB7, set when ATN is asserted.

## Undocumented opcodes are results, not just cycles

`cycle_test.cpp` times LAX, SLO, SRE, RRA, RLA and DCP without checking what
they compute, and Klaus Dormann's suite does not walk them at all. A fast
loader is made of them: Sparkle's GCR decoder, in the drive's zero page, is
almost nothing but LAX, SAX, ALR and SBX. A wrong result there does not crash,
it decodes to the wrong byte and the loader spins for ever.

`cpu_test.cpp` now checks results and flags for eleven of them. They pass,
which rules the opcodes out as the cause of the Sparkle demos hanging and
leaves the disk side, where this emulation is byte granular and VICE is bit
granular.

## The Lab: a deadlock that is a timing relationship, not a wrong line

Reproduces on the host build, and is characterised down to both sides' code.
It is written down because everything about it points at *when* things happen
rather than at any one wrong value, and the next attempt should start here
rather than re-deriving it.

The C64 calls the loader's init at `$3800`. That routine patches itself, then
ends:

```
398D  A9 3F     LDA #$3F
398F  8D 02 DD  STA $DD02      ; DDRA: drive the bus
3992  AD 00 DD  LDA $DD00
3995  10 FB     BPL $3992      ; wait for DATA to be released
3997  60        RTS
```

The drive, meanwhile, is running uploaded code. Its preamble at `$0600` posts
job-queue reads, waits for each to come back `$01`, does `SEI`, sets `DDRB=$7a`
and `PCR=$ee`, and falls into:

```
0640  A2 C0     LDX #$C0
0642  8E 0E 1C  STX $1C0E      ; arm T1 on both VIAs, for later
0645  8E 0E 18  STX $180E
0648  0A        ASL A          ; A is $01 from LDA #$01 at $0628
0649  8D 00 18  STA $1800      ; $02: PB1 high, so DATA is pulled LOW
064C  2C 00 18  BIT $1800
064F  10 FB     BPL $064C      ; wait for PB7, which is ATN asserted
```

So the C64 waits for DATA and the drive waits for ATN, with interrupts off on
the drive (`I=1` measured), so nothing else can move either.

The ordering is the interesting part. Measured with an instruction counter
shared between both CPUs:

    t=3067705   ATN released, the end of the KERNAL's UNLISTEN
    t=3277932   drive reaches $0640 and pulls DATA low
    t=3386053   C64 reaches $3992 and sees DATA=LOW, ATN=high

The C64 arrives **after** the drive has already gone busy. On hardware it has
to arrive before, find DATA still high, return from `$3800`, and only then
assert ATN, which releases the drive. Our drive gets to `$0640` about 110k
instructions too early relative to the C64 -- or the C64 gets there too late.

What has been ruled out, so do not spend time on it again:

- All four bus polarities match Frodo: the C64's output (`~pra & ddra`), the
  drive's DATA/CLK from PB1/PB3, the ATN acknowledge gate, and ATN arriving on
  PB7 set when asserted. `$dd00` bit 7 also reads set when DATA is released,
  as Frodo's `MOS6526_2::ReadRegister` does.
- `A=$01` at `$0640` is correct: it comes from `LDA #$01` at `$0628`, so the
  drive really is meant to pull DATA and wait.
- The undocumented opcodes the drivecode uses are result-tested and pass.
- It is not an interrupt that never fires: the drive has `I=1` there and is
  polling on purpose.

### Measured, and what it rules out

The C64 side, disassembled, is a two-stage handshake:

```
3839  A9 C3     LDA #$C3
383B  8D 00 DD  STA $DD00      ; releases ATN: bit 3 clear
383E  A9 3F     LDA #$3F
3840  8D 02 DD  STA $DD02
3843  2C 00 DD  BIT $DD00
3846  30 FB     BMI $3843      ; wait for DATA to go LOW: the drive saying it is there
3848 ...                       ; then a transfer loop that bit bangs $DD02 alone,
                               ; writing $1f or $0f, which moves CLK between
                               ; driven-zero and input while $DD00 stays $C3
3992  AD 00 DD  LDA $DD00
3995  10 FB     BPL $3992      ; then wait for DATA to be released again
```

That transfer loop is why a line left as an input has to pull low: the loader
never writes `$DD00` again, and toggling `$DD02` is the whole signal.

The drive's jobs, timed on the shared counter (C64 instructions):

    t=3011133  posted b0 seek  track 18   -> ok after   4600
    t=3015837  posted 80 read  track 18   -> ok after  36393
    t=3134112  posted b0 seek  track 17   -> ok after  36813
    t=3170996  posted 80 read  track 18   -> ok after 106852
    t=3277932  drivecode reaches $0640, pulls DATA low
    t=3386053  C64 reaches $3992

ATN is released at t=3067705, which is **before the third job is even posted**.
So no plausible change to job or rotation timing gets the drivecode to its ATN
wait while ATN is still low, and the "the drive is too slow" reading is wrong.
The C64 releases ATN at `$383B`, before it waits for DATA at `$3843`, so on
hardware ATN is long gone by the time the drive pulls DATA either.

Which means the model of that `BIT $1800 / BPL $064C` wait is wrong somewhere,
not the timing around it. On the reading used here -- VIA1 PB7 set when ATN is
asserted -- the loop cannot exit at all, and the demo would hang on hardware
too. It runs on hardware **and in VICE**, so the fault is ours.

### Checked against VICE as well as Frodo, and matching

VICE's own source is worth fetching for this; it is not in the tree. The files
that matter are `src/drive/iec/via1d1541.c`, `src/iecbus.h` and
`src/c64/c64cia2.c` from `github.com/VICE-Team/svn-mirror` under `vice/`.

- The drive reads port B as `(drv_port ^ 0x85) | 0x1a | driveid`, then
  `(PRB & DDRB) | (tmp & ~DDRB)`. The `^ 0x85` inverts DATA, CLK and ATN, so
  **PB7 is set when ATN is asserted**, agreeing with Frodo. `IECBUS_DEVICE_READ_ATN`
  being `0x80` and set in `iecbus_init` is the idle bus *before* that inversion,
  which is easy to misread as the opposite.
- Output bits read back the latch and only input bits read the bus. `Via6522::read`
  does exactly this already.
- The C64 side hands the bus `~byte` where byte is the port A *pin* value, which
  is the same `~pra & ddra` Frodo uses and the same this code now uses.

So the drive's PB7 polarity, the port B read-back rule and the C64's output
formula are all confirmed against two reference emulators and all match. The
disagreement is somewhere else, and it is not any of:

- the four bus polarities, the `$dd00` read, or the port B read-back
- `A=$01` at the drivecode's entry, which is `LDA #$01` at `$0628`
- the undocumented opcodes, which are result-tested
- an interrupt that never fires: the drive has `I=1` there and polls on purpose
- job or rotation timing, per the measurements above

What is left, and what the next attempt should establish first, is where ATN
comes from at all after `$383B` releases it. Over a whole run ATN changes 30
times, all of them during the KERNAL LOAD, and never again. Either the C64 is
meant to assert it later and does not get there, or the drive is meant to reach
`$064c` while it is still low. Instrumenting VICE's own drive PC at that moment
would answer it in one run and is cheaper than any more reasoning from here.
