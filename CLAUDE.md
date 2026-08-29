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

## The Lab hangs looking for a header byte after sync

Reproduces on the host build. The earlier account in this file said the drive
deadlocked at `$064c` waiting for ATN while the C64 waited for DATA. **That was
wrong**, and it is worth saying why: it came from a hot-PC histogram sampled at
one moment, and from a probe that logged only the first thirty ATN transitions
and then every two hundredth, so everything between was invisible. Both made a
working handshake look like a deadlock.

The handshake works. `$3848` writes `#$37` to `$dd02`, which makes ATN an input,
and an input pulls its line low, so ATN asserts and the drive moves on three
instructions later:

    t=3277941  ATN -> LOW  (pra=c3 ddra=37)
    t=3277944  drive leaves the ATN wait, now at $0653
    t=3277953  ATN -> high (ddra=0f)

That also shows why a port bit left as an input has to pull low: this loader
never writes `$dd00` again after `$C3`, and toggling `$dd02` is the whole signal.

Where it actually stops is in the uploaded drivecode at `$05d1`, sampled at
720,917 hits against 42,822 for the byte-ready wait below it:

```
05CF  A0 52     LDY #$52
05D1  2C 00 1C  BIT $1C00
05D4  30 FB     BMI $05D1      ; wait for SYNC
05D6  AD 01 1C  LDA $1C01      ; take the byte that was ready
05D9  B8        CLV
05DA  50 FE     BVC $05DA      ; wait for the next one
05DC  CC 01 1C  CPY $1C01      ; is it $52?
05DF  D0 EB     BNE $05CC      ; no: reset the stack and hunt again
```

It is reading raw GCR and looking for `$52` at a fixed offset after sync. `$52`
is not arbitrary: the header mark `$08` GCR encodes to `01010 01001`, whose
first byte is `0101 0010`. So it wants the first byte of a standard header
block, one byte after the sync ends, and never sees it.

So this is sync and byte alignment on the emulated surface, not the serial bus.
The DOS tolerates the same track because it hunts for the header rather than
demanding it at a fixed offset.

Measured, logging the head at each of the hunt's two reads:

    pc=$05d9  sync=1  headPos=6154     first read, still on the sync mark
    pc=$05df  sync=0  headPos=6159     second read, five bytes later

Between those two reads the drive executes `CLV` and one `BVC` spin, so the
head should have moved **one** byte. It moves five. The loader therefore never
compares the byte it means to, and `$52` never turns up however long it hunts.

That is the bug to chase: byte ready is not pacing one byte per read for a
loop this tight, so a fast reader loses four bytes in five. The DOS never
notices because it re-hunts rather than counting. `Drive1541::countByteReady`
and `DiskController::rotate`, with the `headReadThisByte` interlock between
them, are where the byte is either delivered or skipped, and VICE's
`src/drive/rotation.c` is the reference -- it models rotation at the bit level
where this is byte granular.

## VICE as an oracle, through the MCP build

The plain `x64sc` on this machine renders nothing headless: `-exitscreenshot`
comes back as an all but black frame with `-console`, with `SDL_VIDEODRIVER=dummy`,
under Xvfb, with `-default`, and with a monitor breakpoint driving `screenshot`.
Do not spend time on it again.

What does work is `~/Projects/vice-mcp`, a VICE build with an MCP server compiled
in. The binary is `vice/build-test-with-mcp/src/x64sc` and it needs a display, so
Xvfb is still required:

```bash
Xvfb :99 -screen 0 1024x768x24 &
DISPLAY=:99 x64sc -mcpserver -warp &      # serves http://127.0.0.1:6510/mcp
```

Then POST JSON-RPC 2.0 at `/mcp`: `tools/list` enumerates 64 tools, and
`tools/call` runs them. The useful ones here are `vice_autostart`,
`vice_checkpoint_add` (exec, load or store, so watchpoints too),
`vice_registers_get`, `vice_cia_get_state`, `vice_memory_read`,
`vice_disassemble` and `vice_display_screenshot`.

Three traps, each of which cost a run:

- **`sleep` is unavailable in this environment and `read -t 1 < /dev/zero`
  returns instantly**, so a wait loop built from it does not wait at all.
  `python3 -c "import time; time.sleep(3)"` does.
- **`pkill -f x64sc` matches the shell running it** and kills the caller. Use
  `pkill -x`, or put the commands in a script file.
- A checkpoint pauses the machine, and it stays paused. Poll `vice_ping` for
  `execution` and call `vice_execution_run` again, or nothing moves and every
  reading is of a halted machine.

Only the C64 is exposed: `vice_memory_banks` reports cpu, ram, rom, io and cart,
with no drive bank, so the 1541's RAM and PC cannot be read this way. Comparisons
have to be made on the C64 side.

### What it has already settled about The Lab

At `$3843`, the wait for DATA to go low, VICE reports `port_a=$c3 ddr_a=$3f` --
exactly what this emulator has there. So the C64's side of the handshake is set
up identically in both, and the divergence is after `$3846`, not before it.
VICE cycles through `$3843`/`$3846` repeatedly while the demo runs, and reaches
the demo proper; this emulator gets there too and then sticks at `$3992`.

The next experiment is to checkpoint `$3992` in VICE from before the autostart
and establish whether it is ever executed at all, then trace forward from
`$3846` in both and find the first instruction that differs.
