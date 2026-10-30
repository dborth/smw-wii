<p align="center">
  <img src="Super%20Mario%20War.png" alt="Super Mario War" width="300">
</p>

<h1 align="center">Super Mario War Wii / Wii U</h1>

<p align="center">
  <b>Version 2.0 (unreleased)</b><br>
  A Wii and Wii U port of <a href="https://github.com/mmatyas/supermariowar">Super Mario War</a><br>
  <a href="https://github.com/dborth/smw-wii">github.com/dborth/smw-wii</a> &middot; GPL licensed<br>
  Port author: Tantric (Daryl Borth)
</p>

---

## About

Super Mario War is a fast, Mario-themed multiplayer platform fighter for up to four players: stomp, shell and power-up
your friends across hundreds of maps and several game modes.

Version 2.0 is a **fresh port** of Super Mario War, built from the continued, actively maintained project at
[mmatyas/supermariowar](https://github.com/mmatyas/supermariowar). It replaces the old 1.x port, which was based on
Super Mario War 1.8 beta.

It is built on **SDL3**, using two forks of SDL:

- **Wii:** the Wii/GameCube (libogc2) fork, [dborth/SDL, branch `ogc-port`](https://github.com/dborth/SDL/tree/ogc-port)
- **Wii U:** the Wii U (wut) fork, [dborth/SDL, branch `wut-port`](https://github.com/dborth/SDL/tree/wut-port)

The two versions are separate builds with separate installation steps. Sections below are marked **Wii** or **Wii U**
where they differ, and apply to both where they are not.

---

## Requirements

### Wii

- A Wii with the [Homebrew Channel](https://wiibrew.org/wiki/Homebrew_Channel) installed
- An SD card (about 55 MB free for the game files)
- At least one controller: a GameCube controller, or a Wiimote (optionally with a Nunchuk or Classic Controller)

The game reads its files **from the SD card only**. Loading from a USB drive is not supported.

### Wii U

- A Wii U with [Aroma](https://aroma.foryour.cafe/)
- An SD card (for the `.wuhb`, and for your settings)
- At least one controller: the GamePad, a Wii U Pro Controller, or a Wiimote (optionally with a Nunchuk or Classic
  Controller)

The game files are **bundled inside the `.wuhb`**. There is no separate `data` folder to copy.

---

## Installation

### Wii

Copy two folders to the **root of your SD card**:

| From the download | Copy to | What it is |
|---|---|---|
| `apps/SuperMarioWar-Wii/` | `sd:/apps/SuperMarioWar-Wii/` | The game itself (`boot.dol`, `meta.xml`, `icon.png`) |
| `smw/data/` | `sd:/smw/data/` | All game assets: graphics, sounds, music, maps |

When you are done, your SD card should look like this:

```
sd:/
├── apps/
│   └── smw/
│       ├── boot.dol
│       ├── meta.xml
│       └── icon.png
└── smw/
    └── data/
        ├── filters/
        ├── gfx/
        ├── maps/
        ├── music/
        ├── screenshots/
        ├── scripts/
        ├── sfx/
        ├── tours/
        └── worlds/
```

> **The game assets must be in `sd:/smw/data`.** This location is built into the program; it does not look next to
> the `.dol` and it cannot be changed in a settings file.

If you are upgrading from an older 1.x version, delete your old `sd:/apps/SuperMarioWar-Wii` and `sd:/smw` folders first and copy the
new ones fresh.

### Common mistakes

| Mistake | What happens | Fix |
|---|---|---|
| Putting the `data` folder inside `sd:/apps/smw/` | The game never looks there and reports missing files | Move it to `sd:/smw/data/` |
| Not copying `data` at all (only `apps/`) | Error screen: `Couldn't load sd:/smw/data/...` | Copy the `smw/data` folder to the SD card |
| An extra folder level, such as `sd:/smw/data/data/gfx` | Same missing-file error | `gfx`, `sfx`, `maps`, `music`... must be directly inside `sd:/smw/data/` |
| Only some of the files copied (copy interrupted, or a corrupt unzip) | Missing-file errors, or `Could not load any map music!` | Delete `sd:/smw/data` and copy it again |
| Running from USB, or putting the files on a USB drive | Files not found | Use an SD card. All paths start with `sd:/` |
| Pointing the Homebrew Channel at a different folder | Works, because the data path does not depend on where the `.dol` lives | Nothing to fix, but the data must still be in `sd:/smw/data/` |

### Wii U

Copy one file to your SD card:

| From the download | Copy to | What it is |
|---|---|---|
| `wiiu/apps/SuperMarioWar-WiiU.wuhb` | `sd:/wiiu/apps/SuperMarioWar-WiiU.wuhb` | The game and all of its assets (graphics, sounds, music, maps) |

When you are done, your SD card should look like this:

```
sd:/
└── wiiu/
    └── apps/
        └── SuperMarioWar-WiiU.wuhb
```

Settings are created on the SD card the first time the game saves them (see [Files and settings](#files-and-settings)).
You do not have to create anything.

If you are upgrading, replace `SuperMarioWar-WiiU.wuhb` with the new one. Your settings in `sd:/smw/` are kept.

#### Common mistakes (Wii U)

| Mistake | What happens | Fix |
|---|---|---|
| Copying a `data` folder next to the `.wuhb`, or to `sd:/smw/data/` | Nothing: the game does not read it. It always uses the files inside the `.wuhb` | Delete the extra folder |
| Running a bare `.rpx` instead of the `.wuhb` | `game data not found: fs:/vol/content/data`, because only a `.wuhb` carries the game files | Start `SuperMarioWar-WiiU.wuhb` from the Wii U menu |
| An interrupted copy of `SuperMarioWar-WiiU.wuhb` | The game fails to start, or reports that its game data is incomplete | Copy the file again |

---

## Starting the game

1. **Wii:** start the Homebrew Channel and choose **Super Mario War Wii**.
   **Wii U:** start **Super Mario War Wii U** from the Wii U menu (with Aroma, it is listed with your other homebrew).
2. Wait for the title screen (**Press Any Key To Continue**).
3. Press a button on a connected controller (see [Controllers](#controllers)).

Wake your Wiimotes (press 1+2) before or while the game loads. A controller is picked up the moment it connects, even
after the title screen has appeared.

---

## Controllers

### Supported controllers

**Wii**

- GameCube controllers (all four ports)
- Wiimote held **sideways** (NES style: D-pad on the left, buttons 1 and 2 under your right thumb)
- Wiimote + **Nunchuk**
- Wiimote + **Classic Controller**

**Wii U**

- The **GamePad**
- Wii U **Pro Controller**
- Wiimote held **sideways**
- Wiimote + **Nunchuk**
- Wiimote + **Classic Controller**

### How controllers are assigned

- When a controller connects, it is bound automatically to the **first player that has no controller yet**.
  Plug in or wake controllers in the order you want players to be numbered.
- On the Wii U, the GamePad counts as a controller like any other. It is player 1.
- If a controller disconnects (dead batteries, out of range), that player goes back to the keyboard setup. When the
  controller reconnects, it is bound to the first player without a controller.
- Up to four players can play, even though more than four controllers can be connected.

### Default bindings

These apply the first time you run the game, when no controls file exists yet. (They are decided by the controller
type, not by which port it is in.)

**In a match**

| Action | GameCube | Wiimote (sideways) | Wiimote + Nunchuk | Wiimote + Classic |
|---|---|---|---|---|
| Move left / right | Control stick | D-pad | Nunchuk stick | Left stick |
| Down | Control stick down | D-pad down | Nunchuk stick down | Left stick down |
| Jump | A | 2 | A | A |
| Turbo (run / fire) | B | 1 | B | B |
| Use power-up | X | A | Z | X |
| Pause (start) | Start | + | + | + |
| Cancel | Y | - | - | - |

**In the menus**

| Action | GameCube | Wiimote (sideways) | Wiimote + Nunchuk | Wiimote + Classic |
|---|---|---|---|---|
| Up / Down / Left / Right | Control stick | D-pad | Nunchuk stick | Left stick |
| Select | A | 2 | A | A |
| Cancel | B | 1 | B | B |
| Random | X | A | Z | X |
| Fast scroll | Y | B | C | Y |

Sideways Wiimote: press the D-pad in the direction you want to go on screen.
The GameCube and Classic D-pads, and the Wiimote's own D-pad when a Nunchuk is attached, are not bound by default.
You can bind them yourself, see below.

**Wii U: GamePad and Pro Controller**

The GamePad and the Pro Controller use the same buttons as the Classic Controller column above (the Wii U Classic
Controller layout is identical on the Wii). The Wiimote, Nunchuk and Classic columns above also apply on the Wii U.

| Action | In a match | In the menus |
|---|---|---|
| Move | Left stick | Left stick |
| Down | Left stick down | Left stick down |
| Jump / Select | A | A |
| Turbo (run / fire) / Cancel | B | B |
| Use power-up / Random | X | X |
| Pause (start) | + | |
| Cancel (in a match) | - | |
| Fast scroll | | Y |

The D-pad is not bound by default.

### Changing bindings

Open **Controls** on the main menu to reach the **Player Controls Menu**, where you can rebind every button for every
player.

Your bindings are saved to `sd:/smw/controls.sdl2.bin` (on both consoles) when you start a match and when you exit from the menu.
Once that file exists, **it is used as it is**: the defaults above are not applied again.

### Resetting controls

Delete `sd:/smw/controls.sdl2.bin`. On the next start, each controller gets the defaults for its type again.

Do this if the buttons are not what you expect, for example after switching to a different kind of controller, or
after copying settings from another device or from Dolphin.

---

## Files and settings

On the Wii U, `sd:/smw/` is a folder on the SD card, just like on the Wii. The game assets are not there: they are
inside `SuperMarioWar-WiiU.wuhb`.

| Path | What it is |
|---|---|
| `sd:/smw/data/` | Game assets (read only). **Wii only**; on the Wii U they are inside `SuperMarioWar-WiiU.wuhb` |
| `sd:/smw/options.toml` | Game options, saved when you start a match and when you exit from the menu |
| `sd:/smw/options.bin` | Old options format; read only if `options.toml` does not exist |
| `sd:/smw/controls.sdl2.bin` | Controller bindings |
| `sd:/smw/smw-log.txt` | Crash log. Only written if the game stops because of an error, and replaced each time that happens (see Troubleshooting) |

To reset the game options, delete `options.toml` (and `options.bin`, if you have it).
The `sd:/smw` folder is created automatically if it does not exist, even if you forgot to copy `data` into it (Wii).
Settings cannot be saved if the SD card's write-protect switch is on.

---

## Troubleshooting

### The game stops with an error (Wii U)

If the game stops because of an error, it saves a log to `sd:/smw/smw-log.txt`. The **`Reason:`** line at the top
says what went wrong. Each failure replaces the previous log. If the log cannot be written, the SD card is missing or
write-protected.

| Reason | Meaning | What to do |
|---|---|---|
| `game data not found: fs:/vol/content/data` | The game was started without its bundled files, for example as a bare `.rpx` | Start `SuperMarioWar-WiiU.wuhb` from the Wii U menu |
| `game data incomplete: N expected folder(s) missing` | The `.wuhb` is damaged or incomplete. The log names the missing folders | Copy `SuperMarioWar-WiiU.wuhb` to the SD card again, or download it again |
| `the game stopped with an error: Couldn't load ...` / `Failed to load ...` | A graphics, sound or music file in the `.wuhb` is missing or damaged | Same as above |
| `the game stopped with an error: Could not load any map music!` (or `world music`) | The music files are missing from the `.wuhb` | Same as above |
| `std::terminate(): ...` / `exception escaped main(): ...` / `abort()` / `main() returned N` | The game hit an unexpected error | Send `smw-log.txt` |

A warning line `SD card cannot be read: settings will not be saved` in the log is not an error: the game still runs,
but your options and controls will not be kept. Check that the SD card is inserted and not write-protected.

If the game instead freezes, or the console shows a crash screen, it did not get to write a log: tell us what you were
doing.

### The game stops with an error (Wii)

If it stops because of an error, it shows a screen titled **Super Mario War (Wii) stopped** with the reason and the last lines of the log, and saves the full log to
`sd:/smw/smw-log.txt`. Press any button to return to the Homebrew Channel.

If the screen says it **could not write** the log, the SD card is missing or write-protected. The reason is still on screen.

| Message | Meaning | What to do |
|---|---|---|
| `the SD card cannot be read` | No SD card, or the Wii could not read it | Insert the card (try another one, or a re-formatted one) |
| `game folder sd:/smw/ not found` | The `smw` folder is not on the card | Copy the `smw` folder to the root of the SD card |
| `game data not found: sd:/smw/data` | There is no `data` folder in `sd:/smw` | Copy `smw/data` into `sd:/smw/`. The log lists what is in `sd:/smw/`, which shows when it ended up one level too deep |
| `game data incomplete: N expected folder(s) missing` | Some of `gfx`, `maps`, `music`, `sfx` are missing | Delete `sd:/smw/data` and copy it again. The log names the missing folders |
| `the game stopped with an error: Couldn't load sd:/smw/data/...` | A graphics file is missing or damaged | Check the [folder layout](#installation) and re-copy `sd:/smw/data` |
| `the game stopped with an error: Failed to load sd:/smw/data/...` | A sound or music file is missing or damaged | Same as above |
| `the game stopped with an error: Could not load any map music!` (or `world music`) | The `music` folder is missing or empty | Copy the complete `data` folder, including `music` |
| `exit(N) before the game started` | The program quit while starting up | Re-copy `boot.dol`, and send `smw-log.txt` if it happens again |
| `std::terminate(): ...` / `exception escaped main(): ...` | The game hit an unexpected error | Send the text on screen and `smw-log.txt` |
| `abort()` | The game stopped itself after a serious error | Send `smw-log.txt` |
| `main() returned N` / `exit(N)` | The game ended with an error but gave no details | Send `smw-log.txt` |

If the game instead freezes, or shows a screen about a CPU exception with register values, it did not get to write a
log: tell us what you were doing, and include a photo of the screen.

### Stuck on "Press Any Key To Continue"

- **No controller connected yet.** Wake the Wiimote (press 1+2) and wait for its LED, or check that the GameCube
  controller is plugged in before the Wii was turned on.
- **Old controls file.** If a controls file from another setup is in `sd:/smw/`, your buttons may not match the
  saved bindings. Delete `sd:/smw/controls.sdl2.bin` (see [Resetting controls](#resetting-controls)).
- On the title screen, the Select, Cancel and Random buttons from the tables above all continue.
  That is A, B or X on GameCube and Classic, A, B or Z on Nunchuk, and 2, 1 or A on a sideways Wiimote.

### Buttons are not what I expect

Delete `sd:/smw/controls.sdl2.bin`. See [Resetting controls](#resetting-controls).

### Netplay

**Wii:** the network connection is started only when you enter netplay, not when the game starts. It uses your Wii's
saved network settings and needs a working connection (DHCP). If it fails to start, the game reports that netplay is
unavailable.

**Wii U:** the console connects to your network by itself. When you enter netplay or host a game, the game waits up
to 10 seconds for the connection, and then reports that netplay is unavailable if there is none. It uses the
connection configured in the Wii U's System Settings (Wi-Fi or Ethernet adapter) and needs DHCP. The log in
`sd:/smw/smw-log.txt` records the network state, but only if the game stopped with an error.

Netplay has been compiled in on both consoles, but note that it's totally untested.

---

## Reporting issues

Please report problems at <https://github.com/dborth/smw-wii/issues> and include:

- What you were doing and what you expected
- The text on the error screen, if there was one
- `sd:/smw/smw-log.txt`, if the game wrote one
- Your controller type, and whether you are on a Wii or a Wii U

Problems that also occur in the Windows, Linux or macOS versions of the game belong in the main project:
<https://github.com/mmatyas/supermariowar/issues>.

---

## Changelog

### [2.0 - Unreleased]

* Fresh port of Super Mario War from the continued project at
  [mmatyas/supermariowar](https://github.com/mmatyas/supermariowar). This requires you to replace ALL old files.
* Uses SDL3 (the libogc2 fork at [dborth/SDL, branch `ogc-port`](https://github.com/dborth/SDL/tree/ogc-port)).
* Game files are read from `sd:/smw/data`, and settings are saved in `sd:/smw`. The game runs from `sd:/apps/smw`.
* Controllers are detected as they connect (also after the title screen) and bound to players automatically:
  GameCube controllers, sideways Wiimotes, Wiimote + Nunchuk, Wiimote + Classic Controller. Bindings can be changed in the
  Controls menu and are saved to `controls.sdl2.bin`.
* 16-bit (RGB565) video, for full speed on the Wii.
* Netplay over the Wii's network connection (untested).
* SD card only.
* **Wii U version:** new, built on the Wii U SDL3 fork ([dborth/SDL, branch `wut-port`](https://github.com/dborth/SDL/tree/wut-port)).
  Distributed as a single `SuperMarioWar-WiiU.wuhb` with all game files bundled; settings are saved in `sd:/smw` on the SD card.
  Controllers: GamePad, Pro Controller, sideways Wiimote, Wiimote + Nunchuk, Wiimote + Classic Controller.
  Netplay over the Wii U's network connection (untested).

### [1.4 - August 11, 2010]

* Fresh compile with latest libogc improvements
* USB 2.0 via IOS58 support. Requires that IOS58 be pre-installed.

### [1.3 - January 7, 2010]

* Fresh port of Super Mario War 1.8 beta 2. This requires you to replace ALL old files. Enjoy!

### [1.2 - June 4, 2009]

* Recompiled with latest libogc / libfat / SDL

### [1.1 - May 1, 2009]

* Fix crashing issue (caused by music)
* Hide controller configuration (not implemented)

### [1.0 - April 30, 2009]

* Initial release

---

## Credits and license

- **Super Mario War** was created by Florian Hufsky, building on the original *Mario War* by Samuele Poletto, and
  developed further by Two52, Jared and many others (see the `CREDITS` file in the main project). The continued
  project is maintained at [mmatyas/supermariowar](https://github.com/mmatyas/supermariowar).
- **Wii and Wii U ports:** Tantric (Daryl Borth), using SDL3 with libogc2 (Wii) and wut (Wii U).
- Distributed under the GPL.
