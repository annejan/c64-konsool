# Konsool 64

## Introduction

This application is a functional C64 emulator that run on the [Konsool/Tanmatsu](https://badge.team/docs/badges/konsool/) device.

On the Konsool device, the emulator both outputs audio and video and accepts keyboard input.

## Usage

### Accessing the menu 

Press the 'purple diamond' button on the device at any time to open or close the menu.

### Navigation

The following keys are used:

| Key            | Description                 |
| -------------- | --------------------------- |
| Purple diamond | Show or hide menu           |
| Up/Down        | Move up and down the menus. |
| Enter          | Activate a menu item        |
| ESC            | Go back one menu up         |


Since the menu structure is still being developed, I'm going to not document more details at this time.

## Games / Sofware

### Supported file formats

| Format   | Support                                                          |
| -------- | ---------------------------------------------------------------- |
| **.PRG** | Full. Loaded straight into memory.                               |
| **.T64** | Full. Pick a program from the tape container.                    |
| **.D64** | Read only, mountable as drive 8 so the C64 loads from it itself. |
| .TAP     | Not supported, needs datasette emulation.                        |
| .CRT     | Not supported, needs cartridge ROM banking.                      |

### Loading files

- Put your files in a directory named `c64prg` on the SD card. The directory is
  created for you the first time the emulator starts with a card inserted.

- Open the menu, select 'Load file', and pick a file. Choosing a .t64 or .d64
  opens a second menu listing the programs inside it; press ESC to go back.

- When the C64 screen shows again, type the command 'run' and press enter.

### Two ways to use a .d64

Selecting a disk image gives you a choice.

**Mount as drive 8** hands the whole disk to the C64, which then loads from it
itself:

```
LOAD"$",8        list the directory
LOAD"NAME",8     load a program
LOAD"*",8,1      load the first program
```

This is the one to use for anything that loads more than one part, because the
C64 stays in charge of the loading. The disk stays mounted until you mount a
different one, the same way a real drive stays plugged in across a reset.

**Picking a program from the list** copies that one program straight into
memory, the same way a .prg is loaded. It is quicker for something that loads
in one go, and it is the only option for a .t64.

Files that were never closed properly on the original disk are listed with a
trailing `*`, exactly as a real directory listing marks them, and will usually
fail to load.

### How the drive emulation works, and what it will not do

There is no serial bus hardware in this emulator. Instead the Kernal's own
serial routines are replaced: `LISTEN`, `TALK`, `SECOND`, `TKSA`, `CIOUT`,
`ACPTR`, `UNTLK` and `UNLSN` each get a JAM opcode patched over their first
byte, and the emulator services the call from the mounted image. Their
addresses are read out of the Kernal jump table rather than hardcoded. The
traps are only in place while a disk is mounted; unmount and the ROM is exactly
as it was.

Because every one of the Kernal's higher level routines is built on those eight
primitives, implementing them once covers `LOAD`, `SAVE`, `OPEN`, `CHRIN`,
directory listings and sequential files alike.

What this does **not** cover:

- **Fast loaders.** A fast loader bit-bangs the serial lines directly and
  uploads its own code into the drive, so it never calls the Kernal routines
  that are trapped. That needs a real 1541 with its own CPU, which is a
  separate piece of work; the `IecDevice` interface exists so it can be added
  underneath without disturbing anything above it.
- **Writing.** `SAVE`, `OPEN` for write and the scratch, rename and format
  commands all answer `26,WRITE PROTECT ON`. Reading is unaffected.
- **REL files**, which need side sectors.

### Loading a .prg from BASIC

With no disk mounted, `LOAD"NAME",8` at the C64 prompt still loads `NAME.PRG`
straight from the `c64prg` directory on the card. Mounting a disk takes that
over: while one is mounted, drive 8 is the disk.

### Joystick emulation

The original commodore 64 had two joystick ports namely '1' and '2'.
Because some games use port one and others two, switching the joystick between ports is needed.

In order to enable the Joystick, the 'keyboard joystick' option in the main menu needs to be set to 'yes'.

Selecting the joystick port is done using the 'F5' or 'blue tri-lobe' key.

Indicators of joystick status and port will be added to the software in the future.

#### Joystick key bindings

| key               | Joystick function                |
| ----------------- | -------------------------------- |
| Arrow up          | UP                               |
| Arrow down        | DOWN                             |
| Arrow left        | LEFT                             |
| Arrow right       | RIGHT                            |
| Left SHIFT        | FIRE button                      |
| F5 / Blue diamond | Switch joystick between port 1/2 |

## Perquisites

### Install the build dependencies

#### Debian / Ubuntu: 

```bash
sudo apt-get install git wget flex bison gperf python3 python3-pip python3-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0
```

#### Arch

```bash
sudo pacman -S --needed gcc git make flex bison gperf python cmake ninja ccache dfu-util libusb
``` 

### Setup the the build environment

```bash
make prepare
```

## Build the project

```bash
make build
```

## Running the tests

The image readers and the drive emulation depend on nothing but POSIX file
calls, so they can be built and run on a normal machine without ESP-IDF:

```bash
make -C main/src/images/test run    # .t64 and .d64 parsing
make -C main/src/drive/test run     # the drive and CBM DOS layer
```

The first builds synthetic images, including ones carrying the malformed
headers that turn up in the wild, and checks the right bytes end up at the
right addresses. The second drives the emulated drive through the same bus
commands the Kernal issues, and checks that what comes back is what a real 1541
would return, down to the directory column layout and the status message
format.

## Upload to the Tanmatsu

Fast and easiest way to upload the build

```bash
tools/badgelink.py appfs upload "c64-emu" "C64 Emulator" 0 <project_root>/build/application.bin
```

## Configure clangd

The esp-idf cross compiler has built in include paths, not using this cross compiler will result in clangd complaining about missing include files.
The CMake project project by default already makes a compile_commands.json file, but clangd will not accept any cross compiler without it being white listed.

In order to white list a compiler for clangd to extract the include paths:

```
--query-driver=/**/riscv32-esp-elf/bin/riscv32-esp-elf-gcc
```

In VsCodium this can be done using the following statement in the settings.json

```json
"clangd.arguments": [
  "--query-driver=/**/riscv32-esp-elf/bin/riscv32-esp-elf-gcc"
]
```

## Credits

The drive emulation follows [Frodo](https://github.com/cebix/frodo4) by
Christian Bauer, which does the same thing the same way: patch the Kernal's
serial routines and answer them from a disk image. The Kernal status byte
values come from its `IEC.h`.

The directory listing layout, the status message format and the CBM DOS error
texts are ported from [VICE](https://vice-emu.sourceforge.io/), and the
handling of malformed .t64 containers follows
[t64fix](https://github.com/Compyx/t64fix).

Frodo and VICE are both GPL version 2 or later, which is compatible with this
project's GPL version 3.

## Many many credits for the person who wrote the emulator this is based on

[retrolec](https://github.com/retroelec/T-HMI-C64/commits?author=retroelec)

[T-HMI-C64](https://github.com/retroelec/T-HMI-C64)

## License & Copyright


Modified 2025 Ranzbak Badge.Team

Copyright (C) 2024 retroelec <retroelec42@gmail.com>

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3 of the License, or (at your
option) any later version.
