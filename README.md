# Body Harvest: Recompiled
Body Harvest: Recompiled is a native port of Body Harvest made with [N64: Recompiled](https://github.com/N64Recomp/N64Recomp), using [RT64](https://github.com/rt64/rt64) as the renderer.

### **This repository and its releases do not contain game assets. You need your own copy of the game to play.**

## Table of Contents
* [Installation](#installation)
* [System Requirements](#system-requirements)
* [Features](#features)
* [FAQ](#faq)
* [Building](#building)
* [Libraries Used and Projects Referenced](#libraries-used-and-projects-referenced)

## Installation
1. Download the file for your system from the [latest release](../../releases/latest):

   | System | File | How to run |
   |---|---|---|
   | Windows | `BodyHarvestRecompiled-<version>-Windows.zip` | Extract, run `BodyHarvestRecompiled.exe` |
   | Linux x64 / ARM64 | `BodyHarvestRecompiled-<version>-Linux-X64.tar.gz` / `-Linux-ARM64.tar.gz` | Extract, run `./BodyHarvestRecompiled` |
   | Linux Flatpak | `BodyHarvestRecompiled-<version>-Linux-X64.flatpak` | `flatpak install --user BodyHarvestRecompiled-<version>-Linux-X64.flatpak`, then start it from your app menu |
   | macOS | `BodyHarvestRecompiled-<version>-macOS.zip` | Extract, open `BodyHarvestRecompiled.app` |

   **macOS:** the app is not notarized by Apple, so macOS blocks it the first time. Open it once, then go to **System Settings → Privacy & Security** and click **Open Anyway**.

2. On first start, click **Load ROM** and select your Body Harvest ROM.
3. Click **Start Game**.

### The ROM
* Only the **North American (NTSC-U) N64 release** of Body Harvest works. This is not an emulator; other games and versions are rejected.
* `.z64`, `.n64` and `.v64` files are all accepted; byte order is converted automatically.
* Check your file: SHA-1 `bbb6666f5014a473747ee4145f036d9fb25d7348`.
* The ROM is copied into the game's data folder (see [below](#where-are-the-rom-saves-and-settings-stored)) as `bh.us.z64`, so you only select it once.

## System Requirements
A GPU supporting Direct3D 12.0 (Shader Model 6), Vulkan 1.2, or Metal Argument Buffers Tier 2 is required by the RT64 renderer.

Builds are provided for Windows (x64), Linux (x64 and ARM64, plus a Flatpak) and macOS (Intel and Apple Silicon, macOS 11.0 or newer).

If you have issues with crashes on startup, make sure your graphics drivers are fully up to date.

## Features

#### Easy-to-Use Menus
Graphics settings, input mappings, and audio settings can all be configured with the in-game config menu. The menus can all be used with mouse, controller, or keyboard.

#### Widescreen
Levels are rendered in widescreen when the **Aspect Ratio** graphics setting is **Expand**. The intro, title screen and menus stay at the original 4:3.

## FAQ

#### Where are the ROM, saves and settings stored?
- Windows: `%LOCALAPPDATA%\BodyHarvestRecompiled`
- Linux: `~/.config/BodyHarvestRecompiled`
- macOS: `~/Library/Application Support/BodyHarvestRecompiled`

Saves are in the `saves` subfolder.

#### Can you run this project as a portable application?
Yes, if you place a file named `portable.txt` in the same folder as the executable then this project will run in portable mode. In portable mode, the ROM copy, save files and config files are placed in the same folder as the executable.

## Building
Building is not required to play. Instructions on how to build this project can be found in [BUILDING.md](BUILDING.md).

## Libraries Used and Projects Referenced
* [N64Recomp](https://github.com/N64Recomp/N64Recomp) for statically recompiling the game's code to run natively
* [RT64](https://github.com/rt64/rt64) for the project's rendering engine
* [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime) for replacing the original N64 runtime libraries
* [RecompFrontend](https://github.com/N64Recomp/RecompFrontend) for menus and input handling
* [Harvest Moon 64: Recompiled](https://github.com/HarvestMoon64Recomp/HarvestMoon64Recomp) and [Zelda 64: Recompiled](https://github.com/Zelda64Recomp/Zelda64Recomp) as project templates
