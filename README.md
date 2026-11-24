# ML Emulators for Canon EOS 6D

Magic Lantern emulator modules for Canon EOS 6D (6D.116):

| Module | Core | Systems |
| --- | --- | --- |
| `gb/gb.mo` | Walnut-CGB | Game Boy / Game Boy Color |
| `nes/nes.mo` | QuickNES | NES / Famicom |

Each module is self-contained: menu, ROM scanner, video, audio, and 6D key handling.

## Install

Copy to the camera card:

```text
ML/MODULES/gb.mo
ML/MODULES/nes.mo
ML/GB/*.gb
ML/GB/*.gbc
ML/NES/*.nes
```

Then open `Games → GB` or `Games → NES`.

No ROMs are included.

## 6D controls

| 6D key | GB | NES |
| --- | --- | --- |
| Arrow keys / multicontroller | D-pad | D-pad |
| `SET` | A | A |
| `PLAY` | B pulse | B pulse |
| Hold magnify | B hold | B hold |
| `INFO` | Select | Select |
| `MENU` | Start | Start |
| `Q` | Exit | Exit |

## Video

- GB: `3x`, `4x crop`, `Stretch`; rendering is Direct only.
- NES: `Exact 2x`, `Stretch fullscreen`; rendering is Direct only.

Both modules have a `Volume` menu; audio goes through Canon ASIF-DMA at 48 kHz.

## Build

Requirements: Magic Lantern source tree, xPack ARM GCC, Python for module help strings.

```sh
cp -r gb gb6d
cp -r nes nes6d

cd magiclantern
make -C modules/gb6d -j4
make -C modules/nes6d -j4
```

Outputs:

```text
modules/gb6d/build/gb.mo
modules/nes6d/build/nes.mo
```

## Binaries

| File | Size | SHA-256 |
| --- | --- | --- |
| `gb/gb.mo` | 107,196 | `984F3E6093F9C00F4A8FD71FB73B9290CBEE48271488E5FE2BF566F66B0B0B4A` |
| `nes/nes.mo` | 441,600 | `5EEAC27FFD1BE104F4A9BD00C850B84440D154825DB294A0EDA30C6F5B9958C0` |

Both were validated against the 6D.116 symbol table with 0 missing imports.

## Notes

- GB uses Walnut-CGB plus minigb_apu (MIT).
- NES uses QuickNES 0.7 (GPL-2.0).
- Rendering is intentionally Direct only; Double/Flip buffer modes were removed after 6D bring-up.
- These are homebrew/porting projects; use ROMs you legally own.


