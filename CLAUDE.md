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
make -C main/src/images/test run    # .t64 and .d64 parsing
make -C main/src/drive/test run     # CBM DOS, GCR, the 1541 hardware
make -C main/src/test run           # the 6502 itself
```

They also run in CI on every pull request.

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
from `/c64prg/1541.rom` on the SD card at runtime.
