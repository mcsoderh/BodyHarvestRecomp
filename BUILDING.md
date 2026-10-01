# Building Guide

This guide will help you build the project on your local machine. The process will require you to provide a ROM of the US version of the game.

These steps cover: building the recompiler tools, running them and finally building the project.

## 1. Clone the BodyHarvestRecomp Repository
This project makes use of submodules so you will need to clone the repository with the `--recurse-submodules` flag.

```bash
git clone --recurse-submodules
# if you forgot to clone with --recurse-submodules
# cd /path/to/cloned/repo && git submodule update --init --recursive
```

## 2. Install Dependencies

### Linux
For Linux the instructions for Ubuntu are provided, but you can find the equivalent packages for your preferred distro.

```bash
# For Ubuntu, simply run:
sudo apt-get install cmake ninja-build libsdl2-dev libgtk-3-dev lld llvm clang
```

### Windows
You will need to install [Visual Studio 2022](https://visualstudio.microsoft.com/downloads/).
In the setup process you'll need to select the following options and tools for installation:
- Desktop development with C++
- C++ Clang Compiler for Windows
- C++ CMake tools for Windows

The other tools necessary are `make` and the official LLVM release, both of which can be installed via [Chocolatey](https://chocolatey.org/):
```bash
choco install make llvm
```
The LLVM release is needed because the clang bundled with Visual Studio can't build the game patches, which are MIPS code. The build looks for it in `C:\Program Files\LLVM\bin`. If it is somewhere else, pass `-DPATCHES_C_COMPILER=<path>\clang.exe -DPATCHES_LD=<path>\ld.lld.exe` to CMake.

## 3. Preparing the target ROM
You will need to obtain a NTSC-U N64 Body Harvest ROM.

After that, copy the ROM to the root of the BodyHarvestRecomp repository with this filename:
- `bh.us.z64`

## 4. Applying the dependency patches
A few fixes to the submodules live in `patches/deps/<submodule path>/*.patch`. They are applied automatically when the project is configured, but the recompiler tools are built before that, so apply them once by hand first:

```bash
cmake -P cmake/apply_dep_patches.cmake
```

## 5. Generating the C code

Build [N64Recomp](https://github.com/N64Recomp/N64Recomp) from the `lib/N64ModernRuntime/N64Recomp` submodule. Use this copy rather than an upstream checkout, because `RSPRecomp` needs the patch from the previous step to handle Body Harvest's audio microcode.

```bash
cmake -S lib/N64ModernRuntime/N64Recomp -B build-tools -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang
cmake --build build-tools --target N64RecompCLI RSPRecomp
```

On Windows, run the commands from an "x64 Native Tools Command Prompt for VS 2022" and use `clang-cl` instead:
```bash
cmake -S lib/N64ModernRuntime/N64Recomp -B build-tools -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_C_COMPILER=clang-cl
cmake --build build-tools --target N64RecompCLI RSPRecomp
```

Then, from the repository root, run:
```bash
./build-tools/N64Recomp bh.us.toml
./build-tools/RSPRecomp aspMain.toml
```
On Windows the paths are `build-tools\N64Recomp.exe` and `build-tools\RSPRecomp.exe`.

Run `N64Recomp` again whenever `bh.us.toml` or `bh.us.syms.toml` changes.

## 6. Building the Project

Finally, you can build the project! :rocket:

On Windows, you can open the repository folder with Visual Studio, and you'll be able to `[build / run / debug]` the project from there.

If you prefer the command line or you're on a Unix platform you can build the project using CMake:

```bash
cmake -S . -B build-cmake -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang -G Ninja -DCMAKE_BUILD_TYPE=Release # or Debug if you want to debug
cmake --build build-cmake --target BodyHarvestRecompiled -j$(nproc) --config Release # or Debug
```

On Windows, from the same command prompt as in step 5:
```bash
cmake -S . -B build-cmake -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_C_COMPILER=clang-cl -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake --target BodyHarvestRecompiled
```

## 7. Success

Voilà! You should now have a `BodyHarvestRecompiled` executable in the build directory! If you used Visual Studio this will be `out/build/x64-[Configuration]` and if you used the provided CMake commands then this will be `build-cmake`. You will need to run the executable out of the root folder of this project or copy the assets folder to the build folder to run it.
