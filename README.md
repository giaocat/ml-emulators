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

## License

This project is licensed under the GNU General Public License v2.0; see
[LICENSE](LICENSE).

Third-party components:

- Walnut-CGB and minigb_apu: MIT (`gb/LICENSE-Walnut-CGB`, `gb/LICENSE-minigb-apu`)
- QuickNES: GPL-2.0 (`nes/LICENSE-QuickNES`)

## Notes

- GB uses Walnut-CGB plus minigb_apu (MIT).
- NES uses QuickNES 0.7 (GPL-2.0).
- Rendering is intentionally Direct only; Double/Flip buffer modes were removed after 6D bring-up.
- These are homebrew/porting projects; use ROMs you legally own.



