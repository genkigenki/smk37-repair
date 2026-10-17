# SMK-37 Repair

A small firmware for the M-Vave SMK-37 (Pro, Elite, MKE-P37) that repairs M-Vave's key mapping and keeps the
keyboard's own calibration, wheel values and Bluetooth pairings.

It is for one case: back on M-Vave's firmware, every key reads "C" and the synth is silent. Then the stock firmware's
key mapping in its user area is damaged. M-Vave's firmware rebuilds a clean one when its end marker is missing, but
that rebuild erases the calibration, the pairings and the wheel values next to it (it erases whole 64 KiB blocks).
This firmware keeps them across the rebuild.

**Use it from the web page:** <https://genkigenki.github.io/smk37/help.html>, Repair (Chrome or Edge). The page does
all of it by itself, in about 10 minutes, with M-Vave's V016 file that you download from M-Vave:

1. installs this firmware, which copies the calibration (0xC1000), the pairings (0xC0000) and the wheel values to a
   free area (0xF0000-0xF2FFF), checks the copy and then releases the stock marker (0xEA000);
2. installs M-Vave's V016, which writes a clean factory mapping on its first start;
3. installs this firmware again;
4. it writes the calibration, the pairings and the wheel values back, checks them and erases its copy;
5. installs M-Vave's V016 again.

If it stops at any point, press Repair again: it goes on where it stopped.

## What it writes, and when

- Nothing at start: it shows its state and answers the page.
- Only on a command from the page (or SEQ PLAY held for 3 s on the unit):
  - **save**: erases and writes its own copy at 0xF0000-0xF2FFF, then erases the marker sector 0xEA000;
  - **restore**: only after M-Vave's firmware has rebuilt its mapping. It puts the pairing and calibration sectors back
    from its copy and verifies them. Over other data (M-Vave's firmware writes its default calibration after a
    while) only when the copy was made on the same flash chip (its unique ID, saved with the copy); otherwise only
    into erased sectors, and anything else is left as it is and reported. It patches the 10 wheel bytes in the stock
    globals (0xC2000), and erases its copy only after everything is verified.
- Never the boot area, the firmware area outside its own install, USR or the last sector.

A power cut at any step leaves a state the page can go on from (tested on a simulated flash, a cut after every step).

## Protocol

SysEx, manufacturer 0x7D, on the firmware's USB MIDI port "SMK-37 Repair":

| | |
|---|---|
| query | `F0 7D 52 45 50 01 F7` |
| save | `F0 7D 52 45 50 03 F7` |
| restore | `F0 7D 52 45 50 04 F7` |
| reply | `F0 7D 52 45 50 02 <state> <flags> F7` |

States: 0 busy, 1 ready to save, 2 saved and released (install V016 next), 3 copy kept but the mapping not rebuilt
yet, 4 copy kept and the mapping rebuilt (ready to restore), 5 restored (install V016 to finish), 0x10 flash error,
0x11 refused. Flags: bit 0 calibration valid now, bit 1 the copy holds a calibration, bit 2 wheel values,
bit 3 pairing refused, bit 4 calibration refused.

## Build

Linux or WSL with JieLi's toolchain, as for [fm1-base](https://github.com/charlesvestal/fm1-base): `./build.sh`. The
packages land in `build/repair37/`. `app/tests/run_tests.sh` runs the tests.

## Licence

MPL-2.0. Built on [fm1-base](https://github.com/charlesvestal/fm1-base) (Charles Vestal) and
[felucca-bsp](https://github.com/hugelton/felucca-bsp) (Leo Kuroshita, Hügelton Instruments), included in `base/`
with their licences. Unofficial; not made by or affiliated with M-Vave.
