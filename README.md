# microb
MicroBuilder for Embedded devices: generates a `makefile` for AVR and STM32 firmware from a short `microb.ini`.

## Build and install

```sh
make            # bin/microb + bin/mcu (MCU database)
make test       # smoke tests, no toolchains needed
make install    # $(PREFIX)/bin/microb + $(PREFIX)/share/microb/mcu, PREFIX=/usr/local by default
```

The MCU database is looked up in `./mcu` of the project first, then in `$MICROB_HOME/mcu`,
then next to the executable (`<bin>/mcu` or `<bin>/../share/microb/mcu`).

## Usage

```sh
mkdir blink && cd blink && mkdir src
microb          # first run creates a microb.ini template
# set mcu_name (and what the MCU requires) in microb.ini
microb          # writes makefile
make            # build
make flash      # upload
make size
make clean
```

Sources: `src/*.c`, `src/*.cpp`, `src/*.s`, `src/*.S`; headers: `include/`.
Run `microb` again after editing `microb.ini`.

### AVR

```ini
mcu_name = atmega328p
mcu_freq = 16000000UL

# needed only for 'make flash', can also be passed: make flash PORT=/dev/ttyUSB0
programmer = arduino
port = /dev/ttyUSB0
```

### STM32

```ini
mcu_name = stm32f103c8
cmsis_core = ../CMSIS_5                   # contains CMSIS/Core/Include
cmsis_device = ../cmsis_device_f1         # contains Include and Source/Templates
upload = st-flash                         # or openocd, stm32flash
```

The linker script is generated into `build/<mcu>.ld`; set `linker_script` to use your own.

### Keys

Any key from the MCU files can be overridden in `microb.ini`; it becomes an upper-case makefile variable
(`cflags` -> `CFLAGS`, aliases: `mcu_name` -> `MCU`, `compiler` -> `CC`, `cppflags` -> `CXXFLAGS`).
Values may reference other variables: `$(MCU)`, `$(CURDIR)`, environment variables.

`key = value` replaces the value, `key += value` appends to the value from the MCU files:

```ini
cflags += -g -DDEBUG
```

## MCU database

`mcu/<group>/<mcu>.ini` describes one MCU and names its `family`;
`mcu/family/<family>.ini` holds the shared settings, families may inherit other families.
`require = key ...` lists keys that must be set; `[upload.<method>]` sections apply when `upload = <method>`,
their `require` is checked by `make flash`.
