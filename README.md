# DXUP
## A D3D9 and D3D10 -> D3D11 Translation Layer

### Download the latest Windows builds from [GitHub Actions](https://github.com/mthsk/dxup/actions/workflows/build-windows.yml).

Open a successful workflow run for `master` and download the appropriate artifact under **Artifacts**:

- **`dxup-windows-x86`** for 32-bit games and applications.
- **`dxup-windows-x64`** for 64-bit games and applications.

Each artifact contains the Windows DLLs. GitHub sign-in may be required to download workflow artifacts, which expire after a limited retention period.

### What's the point?
 - The main reason is for [DXVK](https://github.com/doitsujin/dxvk), a D3D11->Vulkan translation layer. I'm sure where you can see where this is going... (linux, wine stuff.)
 - You can use this if you're a lazy gamedev and want to get access to some D3D11 features by querying the interfaces (and get some extensions, coming soon!)
 - You write a D3D9Ex game and don't want to deal with sync and other issues for VR.
 - You write a D3D9 game and don't want to deal with D3D9Ex's pool changes and therefore can't get shared resources.

D3D10 support is now deprecated on Linux platforms and won't be installed with the script or the verb as DXVK now implements this.

### How do I use it?
Download the build for your game's architecture from the [Windows build workflow](https://github.com/mthsk/dxup/actions/workflows/build-windows.yml). Expand a successful run's **Artifacts** section, download `dxup-windows-x86` or `dxup-windows-x64`, and extract `d3d9.dll` next to the game executable.

Use the x86 DLL for 32-bit games, and the x64 DLL for 64-bit games. These are CI builds, not tagged releases.

### How do I build it?
**Simple (Linux Only):**
Use ``./package-release.sh master /your/target/directory --no-package`` to automagically.

**Advanced (Windows & Linux):**

You can use meson to build the DLLs:

Windows: ``meson --backend vs2017 --buildtype release build`` in a Visual Studio x86/x64 Command Prompt depending on the arch you wish to build.

Linux: ``meson --cross-file build-win64.txt --buildtype release --prefix /your/dxup/directory build.w64 `` and vice versa for each arch (change 64s to 32s.)

### Screenshots

#### D3D10 (before DXVK got D3D10 support)
![Crysis Warhead Menu](https://i.imgur.com/q1l2gLb.png)
![Crysis Warhead Game](https://i.imgur.com/7yY5bZy.jpg)
![Crysis Warhead Game](https://i.imgur.com/eJbUdxK.jpg)
![Just Cause 2](https://i.imgur.com/mu57Z2O.jpg)
![Just Cause 2](https://i.imgur.com/Q6FoDvj.jpg)
![Just Cause 2](https://i.imgur.com/jGgGrYt.jpg)

# Have fun! 🐸

