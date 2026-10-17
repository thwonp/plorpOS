# Installing on an Anbernic RG Rotate (alpha)

plorpOS on the RG Rotate runs on **Armbian** (Debian trixie, the community
build for the RG Rotate) from a microSD card. Stock Android stays on the
device's own storage: with the card out, the Rotate starts Android as before.

**Alpha.** The card image is made from a set-up device (`mk/rotate-image.sh`)
and carries that device's Wi-Fi settings, root password and ssh key. Use it
on your own Rotate; don't hand it out.

## 0. Once per device: the Armbian boot loader

The Rotate only starts a card when its boot loader slots hold Armbian's SPL
in place of Anbernic's. That is written over USB from the device's download
mode (`spd_dump`), after a full backup of the device's storage. It is done on
the development device; the steps are in the port's plan
(`plorpos-3yv.1`, `.2`) and in Armbian's notes for the board.

## 1. The card

Write `plorpos-rotate.img` to a microSD card of 8 GB or more, as any disk
image (`dd`, balenaEtcher, Raspberry Pi Imager). Check it first against
`plorpos-rotate.img.sha256`.

## 2. First start

Put the card in and turn the Rotate on. **With a USB cable plugged in it stops
at the charging screen: hold Power** to go on. Unplugged, it starts on its own.

The first start makes the data partition: an exFAT partition named
**PLORPOS** across the rest of the card, with `TortOS/` (plorpOS itself) and
empty `Roms/ Bios/ Saves/ Music/`. It takes a few seconds more than later
starts. Then the shelf comes up.

## 3. Your games and files

Turn the Rotate off, put the card in a computer, and copy your files onto the
**PLORPOS** partition, laid out as on the other plorpOS devices:

```
Roms/<system>/   games, one folder per system (see TortOS/systems.cfg)
Bios/            BIOS files, and pico8_64 + pico8.dat for native PICO-8
Music/           Muse's music
Saves/           saves, per console
```

## 4. Controls

Open: the buttons as on the other devices. Menu held with Vol+/Vol- sets the
brightness.

Closed (the screen swivelled over the buttons): the picture turns with it,
and the screen's touch does what the buttons did:

| Touch | Does |
|---|---|
| tap | A |
| swipe | the d-pad (a longer swipe moves further) |
| two-finger tap | B |
| long press | X (a game's info) |
| three-finger tap | Menu |
| long press in a game | the in-game menu |

The **side key** is Muse's: tap to play or pause, hold for the previous track,
double tap for the next - on the shelf and in a game.

## Updating

Copy a newer `TortOS/` folder over the one on the PLORPOS partition.
