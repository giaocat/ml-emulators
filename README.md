# ML Emulators for Canon EOS 6D

Magic Lantern emulator modules for **Canon EOS 6D (firmware 6D.116)**:

| Module | Core | Systems |
| --- | --- | --- |
| `gb.mo` | Walnut-CGB | Game Boy / Game Boy Color |
| `nes.mo` | QuickNES | NES / Famicom |

Each module is self-contained: menu, ROM scanner, video output, 48 kHz audio,
and 6D key handling. The modules use direct core calls; no emulator frontend
or external runtime is required.

---

## 1. Requirements

### Camera

- Canon EOS 6D with firmware **6D.116**
- Magic Lantern installed and bootable
- SD card with the usual ML directory layout:

```text
ML/
  MODULES/
  GB/
  NES/
  LOGS/
```

- Legally obtained `.gb`, `.gbc`, or `.nes` ROMs. No ROMs are included.

### Build host

- Magic Lantern source tree
- ARM cross compiler: **xPack GNU Arm Embedded GCC 15.2.1** recommended
- `make`
- Python 3 with `docutils` (for `readme2modulestrings.py`)

On Windows, Git for Windows/MSYS provides `sh`, `make`, and the basic Unix
tools. On Linux/macOS use the normal build dependencies from the Magic
Lantern documentation.

---

## 2. Install from a Release

1. Open the release page:

   https://github.com/giaocat/ml-emulators/releases

2. Download:

   ```text
   gb.mo
   nes.mo
   ```

3. Copy them to the camera card:

   ```text
   ML/MODULES/gb.mo
   ML/MODULES/nes.mo
   ```

4. Put ROMs here:

   ```text
   ML/GB/*.gb
   ML/GB/*.gbc
   ML/NES/*.nes
   ```

5. Boot the camera with Magic Lantern.

6. Open:

   ```text
   Games → GB
   Games → NES
   ```

Do not rename the modules. Remove any older/duplicate NES or GB modules from
`ML/MODULES/` to avoid duplicate menu entries.

---

## 3. Usage

### GB / GBC

1. Put ROMs in `ML/GB/`.
2. Open `Games → GB`.
3. Select `ROM` and choose a file, or leave it on the first valid entry.
4. Select `Start GB`.
5. In game:
   - `Q` exits back to the camera.
   - `MENU` is GB Start.
   - `INFO` is GB Select.

GB menu options:

| Option | Values | Meaning |
| --- | --- | --- |
| `ROM` | `.gb` / `.gbc` files | Selected ROM, up to 32 files |
| `Video` | `3x`, `4x crop`, `Stretch` | 480×432 centered, 640×480 cropped, or 720×480 stretched |
| `Audio` | `OFF`, `ON` | GB APU audio |
| `Volume` | `1`–`10` | ASIF output volume |
| `Debug logging` | `OFF`, `ON` | Write `ML/LOGS/GB6D.LOG` |
| `Clear GB logs` | action | Delete the GB log |
| `Reset settings` | action | Restore defaults |

The GB core runs at its native ~59.7 fps. Complex scenes can briefly reduce
the frame rate; rendering is Direct only.

### NES

1. Put iNES ROMs in `ML/NES/`.
2. Open `Games → NES`.
3. Select `ROM` (`Auto` uses the first valid file).
4. Select `Start NES`.
5. In game:
   - `Q` exits.
   - `MENU` is NES Start.
   - `INFO` is NES Select.

NES menu options:

| Option | Values | Meaning |
| --- | --- | --- |
| `ROM` | `.nes` files | Selected ROM, up to 32 files |
| `Video` | `Exact 2x`, `Stretch fullscreen` | 512×480 centered or 720×480 stretched |
| `Audio` | `OFF`, `ON` | NES APU audio |
| `Volume` | `1`–`10` | ASIF output volume |
| `Debug logging` | `OFF`, `ON` | Write `ML/LOGS/NES*.LOG` |
| `Clear NES logs` | action | Delete NES logs |
| `Reset settings` | action | Restore defaults |

Rendering is Direct only. If a scene falls behind, the module may skip a small
number of display updates but continues running the core.

### 6D controls

| 6D key | GB | NES |
| --- | --- | --- |
| Arrow keys / multi-controller | D-pad, including diagonals | D-pad, including diagonals |
| `SET` | A | A |
| `PLAY` | B (short pulse) | B (short pulse) |
| Hold magnify / Zoom In | B hold (run/fire) | B hold (run/fire) |
| `INFO` | Select | Select |
| `MENU` | Start | Start |
| `Q` | Exit emulator | Exit emulator |

The 6D sends one combined `UNPRESS_UDLR` event for direction release; the
modules hold the direction until that event, so diagonals work correctly.

### Settings storage

Settings persist in Magic Lantern config under:

```text
games.gb6d.*
games.nes6d.*
```

Changing ROMs or options does not modify the ROM files.

### Logs

```text
ML/LOGS/GB6D.LOG
ML/LOGS/NES-<rom>.LOG
ML/LOGS/NESR-<rom>.LOG
```

Enable `Debug logging` before starting a session. Button callbacks do not
perform card I/O; logs are written from the emulator task.

---

## 4. Build from Source

### 4.1 Get the sources

```bash
git clone https://github.com/giaocat/ml-emulators.git
```

You also need a Magic Lantern source tree that supports the 6D.116 build.

### 4.2 Put the modules into the ML tree

From the ML source root:

```bash
cp -r /path/to/ml-emulators/gb  modules/gb6d
cp -r /path/to/ml-emulators/nes modules/nes6d
```

The resulting layout must be:

```text
magiclantern/
  modules/
    gb6d/
      Makefile
      gb6d.c
      gb_core.c
      ...
      walnut/
    nes6d/
      Makefile
      nes6d.c
      nes_core_qnes.cpp
      qnes/
      ...
```

The Makefiles use `TOP_DIR = ../..`, so they must stay two levels below the
Magic Lantern root.

### 4.3 Toolchain

Put the ARM toolchain on `PATH`, for example:

```bash
export PATH=/opt/xpack-arm-none-eabi-gcc-15.2.1/bin:$PATH
```

If your ML build uses out-of-tree toolchain variables, pass them explicitly:

```bash
make -C modules/gb6d \
  ARM_BINPATH=/opt/xpack-arm-none-eabi-gcc-15.2.1/bin \
  PYTHON=python3 \
  READELF=/opt/xpack-arm-none-eabi-gcc-15.2.1/bin/arm-none-eabi-readelf \
  -j4
```

Python needs `docutils`:

```bash
python3 -m pip install docutils
```

### 4.4 Build GB

```bash
cd magiclantern
make -C modules/gb6d -j4
```

Output:

```text
modules/gb6d/build/gb.mo
```

The build compiles:

- `gb6d.c` — menu, ROM scanner, BMP output, ASIF audio
- `gb_core.c` — Walnut-CGB adapter
- `walnut/walnut_cgb.h` — Walnut-CGB core
- `walnut/minigb_apu.c` — 48 kHz APU
- `gb_rom.c`, `gb_audio_ml.c`, `gb_config.c`

### 4.5 Build NES

```bash
make -C modules/nes6d -j4
```

Output:

```text
modules/nes6d/build/nes.mo
```

NES uses C++ for QuickNES, with:

- `-fno-exceptions`
- `-fno-rtti`
- `-fno-threadsafe-statics`
- `-DBLARGG_NEW=new`
- static `libm`/`libgcc` linking for Blip_Buffer

### 4.6 Clean rebuild

```bash
rm -rf modules/gb6d/build modules/nes6d/build
make -C modules/gb6d -j4
make -C modules/nes6d -j4
```

### 4.7 Install the build

```bash
cp modules/gb6d/build/gb.mo  /media/CARD/ML/MODULES/gb.mo
cp modules/nes6d/build/nes.mo /media/CARD/ML/MODULES/nes.mo
```

On Windows the card path is usually a drive letter, for example:

```text
E:\ML\MODULES\gb.mo
E:\ML\MODULES\nes.mo
```

---

## 5. ROM Layout

```text
ML/
  GB/
    game1.gb
    game2.gbc
  NES/
    game1.nes
```

Rules:

- The scanner lists at most **32** files per directory.
- Extensions are checked case-insensitively.
- GB files must contain a valid GB header and Nintendo logo.
- NES files must be standard iNES images (`NES\x1a` header).
- Subdirectories are not scanned.
- ROMs are read into RAM for the session; the originals are not modified.

---

## 6. Troubleshooting

### Module does not appear in the menu

- Confirm the file is exactly:

  ```text
  ML/MODULES/gb.mo
  ML/MODULES/nes.mo
  ```

- Confirm the camera is running the 6D.116 ML build.
- Remove duplicate/old modules from `ML/MODULES/`.
- Reboot the camera after replacing a module.

### `No ROMs in ML/GB` / `No ROMs in ML/NES`

- Check the directory name and extension.
- GB: `.gb` or `.gbc`, valid header.
- NES: `.nes`, standard iNES header.
- The scanner only looks in `ML/GB/` or `ML/NES/`, not subdirectories.
- There is a 32-file limit.

### `GB core rejected ROM` / `NES core rejected ROM`

- GB: corrupted or non-GB file, unsupported header.
- NES: malformed iNES, unsupported mapper, or non-iNES file.
- Check `ML/LOGS/GB6D.LOG` or `ML/LOGS/NES-*.LOG` with debug enabled.

### Black screen

- Both modules render Direct to the camera BMP buffer. Make sure no other
  module is taking over the display.
- Try another ROM known to work.
- Reboot after replacing a module.

### No sound

- Set `Audio = ON` and `Volume` above 1.
- Check that the camera is not in a state where ASIF audio is unavailable
  (for example, some recording/playback modes).
- Check the log for audio initialization errors.

### Slowdown in complex scenes

GB/NES run on the camera CPU. Heavy effects can exceed one frame budget.
Rendering is already Direct only; there is no Double/Flip option to change.

### Where are the logs?

```text
ML/LOGS/
```

Use `Debug logging = ON`, reproduce the issue, then read the newest
`GB6D`/`NES` log.

---

## 7. License

This project is licensed under the **GNU General Public License v2.0**; see
[LICENSE](LICENSE).

Third-party components:

- Walnut-CGB and minigb_apu: MIT
  (`gb/LICENSE-Walnut-CGB`, `gb/LICENSE-minigb-apu`)
- QuickNES: GPL-2.0 (`nes/LICENSE-QuickNES`)

Magic Lantern itself is licensed separately. These modules are homebrew
ports; use only ROMs you legally own.
