NES 550D / 6D
=============

:Author: Magic Lantern contributors
:License: GPL-2.0
:Summary: Experimental NES module for Canon EOS 550D and 6D.

Place legally obtained iNES ROM files in ``ML/NES/``. The module lists at
most 32 ``.nes`` files alphabetically. It contains no ROMs or commercial game
data. ``Auto`` selects the first valid file. The selection and other options
are persistent.

``Games > NES > Start NES`` runs iNES games through the QuickNES core
(GPL-2.0), covering NROM, UxROM, CNROM, MMC1, MMC3, MMC5, VRC, FME-7 and
most other common mappers. Its centered
512x480 plane maps every 256x240 NES pixel to an exact 2x2 display block.
QuickNES implements the 6502 CPU, PPU background/sprite rendering, controller
input, OAM DMA and full APU audio at 48 kHz.

Controls (550D)
---------------

* Arrows: D-pad
* SET: A
* PLAY: B
* DISP: Select
* MENU: Start
* Q: exit

Controls (6D, build with ``NES_6D=1``)
------------------------------------

* Arrows / multi-controller: D-pad, including diagonals
* SET: A
* PLAY: B (140 ms pulse)
* Hold magnify / Zoom In: B (hold for running)
* INFO: Select
* MENU: Start
* Q: exit


Compatibility
-------------

Audio volume is set by ``Audio`` (ON/OFF) and ``Volume`` (1 to
``ASIF_MAX_VOL``; 10 on the 6D) in the ``Games > NES`` menu. Changes apply
on the next ``Start NES``.

The QuickNES core accepts standard iNES images and supports the common
NROM/UxROM/CNROM/MMC1/MMC3/MMC5/VRC/FME-7 mapper families. Unsupported
mappers and malformed ROMs are rejected with a clear message. PAL timing,
save states and battery-backed persistence are not exposed by the NES550D
shell yet.

Debug logging
-------------

``Debug logging`` is persistent and defaults to ``OFF``. When enabled, session
messages go to ``ML/LOGS/NES550D.LOG`` and buffered raw input events are
written to ``ML/LOGS/NESRAW.LOG`` after the session. Button callbacks never
perform card I/O. ``Clear NES logs`` removes only ``ML/LOGS/NES*.LOG`` and
never removes ROMs or configuration.

Performance diagnostics
-----------------------

Each session starts a fresh structured emulator log containing the selected
ROM and iNES header, configuration, lifecycle checkpoints, early and periodic
frame progress, CPU/PPU/display/audio performance, raw-event overflow counts,
core errors and shutdown state. This mirrors Doom's diagnostic workflow while
keeping all storage access outside input and audio callbacks.

This experimental module supports Canon EOS 550D.109 and, with the
``NES_6D=1`` keymap, Canon EOS 6D.116. Back up the SD card before installing
experimental software.
