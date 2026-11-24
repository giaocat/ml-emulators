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
cp -r gb gb550d
cp -r nes nes550d

cd magiclantern
make -C modules/gb550d GB_6D=1 -j4
make -C modules/nes550d NES_6D=1 -j4
```

Outputs:

```text
modules/gb550d/build/gb.mo
modules/nes550d/build/nes.mo
```

## Binaries

| File | Size | SHA-256 |
| --- | --- | --- |
| `gb/gb.mo` | 107,292 | `6A45D4043494F79ABFD4B00A018F50ADD76720FEFB81B0697612305E46D0EDB5` |
| `nes/nes.mo` | 442,220 | `E6FC31FBCC92D2D7A210882BED18C123E66EB1E2E58D707AEF4A10BA29183BCB` |

Both were validated against the 6D.116 symbol table with 0 missing imports.

## Notes

- GB uses Walnut-CGB plus minigb_apu (MIT).
- NES uses QuickNES 0.7 (GPL-2.0).
- Rendering is intentionally Direct only; Double/Flip buffer modes were removed after 6D bring-up.
- These are homebrew/porting projects; use ROMs you legally own.
