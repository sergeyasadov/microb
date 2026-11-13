# microb

Fast generator of build files (Makefile, CMake, xmake) for MCU firmware from one `microb.ini`.
AVR and STM32 out of the box, any GCC-like toolchain by hand. Linux, macOS, Windows.

## Build and Install

### Build
```sh
git clone https://github.com/sergeyasadov/microb.git
cd microb
make
```

### Install
> [!NOTE]
> `$(PREFIX)/bin/microb` + `$(PREFIX)/share/microb/mcu`, `PREFIX=/usr/local`
```sh
make install
```

## Quick start

Create project
```sh
mkdir -p blink/src && cd blink
```

Create config
```sh
microb
```

> [!NOTE]
> 1. The first run creates a `microb.ini` template.
> 2. Set `mcu_name` (plus what the MCU requires, microb tells which keys).
> 3. Run `microb` again after each edit of `microb.ini`, then build.

Sources: `src/*.c *.cpp *.s *.S`, headers: `include/`.

## microb.ini

| Group     | Keys                                                                 |
|-----------|----------------------------------------------------------------------|
| project   | `project_name` `generator`                                           |
| MCU       | `mcu_name` `family` `mcu_freq`                                       |
| dirs      | `source_dir` `include_dir` `build_dir` `bin_dir`                     |
| toolchain | `compiler` `cxx` `objcopy` `size`                                    |
| flags     | `defines` `includes` `cflags` `cppflags` `asflags` `ldflags`         |
| linking   | `linker_script` `extra_sources`                                      |
| upload    | `upload` `programmer` `port` `baud`                                  |

- `key = value` replaces the MCU default, `key += value` appends to it.
- Empty values are ignored. Any key of the MCU files can be set.
- Values: `$(VAR)`, `$(if cond,then[,else])`, environment variables, `$(CURDIR)`.

## Example

### AVR
```ini
mcu_name = atmega328p
mcu_freq = 16000000UL
programmer = arduino
port = /dev/ttyUSB0
cflags += -g
```

### STM32
```ini
mcu_name = stm32f103c8
cmsis_core = ../CMSIS_5
cmsis_device = ../cmsis_device_f1
upload = st-flash
```

## Generators

| `generator` | File             | Build                                            | Flash                                | `compile_commands.json` |
|-------------|------------------|--------------------------------------------------|--------------------------------------|-------------------------|
| `makefile`  | `makefile`       | `make`                                           | `make flash`                         | project dir             |
| `cmake`     | `CMakeLists.txt` | `cmake -B build -G Ninja && cmake --build build` | `cmake --build build --target flash` | `build/`                |
| `xmake`     | `xmake.lua`      | `xmake`                                          | `xmake build flash`                  | project dir             |

- Several at once: `generator = makefile cmake`; `microb -g cmake` overrides the key.
- Also: `make size`, `--target size`, `xmake build size`.
- Upload keys are checked only when flashing and can be passed later:
  `make flash PORT=...`, `cmake -DPORT=...`, `xmake f --port=...`.
- Linker script: `build/<mcu>.ld` from the family template, or your own `linker_script`.
- clangd with a cross compiler: `clangd --query-driver='/path/to/bin/*-gcc,/path/to/bin/*-g++'`.

## MCU lookup

1. `mcu/<group>/<mcu>.ini` in the database.
2. `family` from `microb.ini`.
3. A family whose `match` fits the name (`family/avr.ini`: `match = atmega* attiny*`).
4. `microb.ini` only: set `compiler`, `objcopy`, `output_ext`, `arch_flags`...

Database search order: `./mcu`, `$MICROB_HOME/mcu`, `<bin>/mcu`, `<bin>/../share/microb/mcu`.
A project's `./mcu/family/<name>.ini` replaces the installed one.

## MCU database

| Family | Coverage                                                                                   |
|--------|--------------------------------------------------------------------------------------------|
| AVR    | every `atmega* attiny* atxmega* at90* ata* avr*` by `match`                                |
| STM32  | 1612 chips: C0 F0 F1 F2 F3 F4 F7 G0 G4 H5 H7 H7RS L0 L1 L4 L5 U0 U3 U5 WB WB0 WBA WL WL3 |

STM32 data: Keil/ST device packs, sizes checked against ST open pin data, startup/system files and
defines against ST `cmsis-device-*`. RAM is the largest contiguous SRAM block (H7: AXI SRAM).

- `mcu/<group>/<mcu>.ini`: per-chip data and `family`.
- `mcu/family/<family>.ini`: toolchain, flags, `match`, may inherit another `family`.
- `require = keys`: must be set. `[upload.<method>]`: applies when `upload = <method>`, its `require` is checked when flashing.

## Windows

microb builds with MinGW-w64 (MSYS2) or `zig cc`. The generated `makefile` needs a POSIX shell
(MSYS2, Git Bash, xPack Build Tools), CMake and xmake don't.
