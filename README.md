# BFN AutoUnlock - source code

Source for the DLL and the injector shipped with the Nexus release
**"One-Click Unlock All Items"** for *Plants vs. Zombies: Battle for Neighborville*.

This repository exists so that anyone - players, and Nexus Mods staff reviewing
the upload - can read exactly what the files do before running them. Both files
that ship as binaries, `AutoUnlock.dll` and `inject_autounlock.exe`, are built
from the sources here, and both builds are reproducible.

---

## ⚠️ Read this before anything else: the Base Mod is required

One-Click Unlock All Items does not work on its own. It needs the Base Mod
already installed:

<https://github.com/MeteorFlower/BFN-Base-Mod-DLL>

The Base Mod is what lets the game reach a map. Without it the game never gets
that far, and **granting then fails silently: no error, nothing granted, and
the log looks completely normal.** This is by far the most common way
One-Click Unlock All Items appears to be broken.

---

## ⚠️ Why antivirus tools may complain

**1. The DLL is injected into a running process.**

The injector - `inject_autounlock.exe`, built from `inject_autounlock.py` -
opens the game process and starts a thread inside it that calls
`LoadLibraryW`. That is what every DLL injector does - and it is also a
technique malware uses, so it trips pattern-based scanners. There is no way to
inject a DLL without tripping them.

**2. The DLL writes into the game's memory.**

It builds the game's own `DebugGrantItemsRequest` objects inside the game
process and hands them to the game's message pipeline. Writing into another
process is the other half of what the scanners look for.

**3. What this does *not* do.**

- It does **not** connect to the network. There is no socket, no HTTP, no WinINet.
- It does **not** read, collect, or transmit any personal data.
- It does **not** touch any process other than the game.
- It does **not** modify any file. It writes one log file, `unlock_log.txt`,
  next to itself.
- It does **not** patch game code. The Base Mod does that. This one only sends
  the game a message it already knows how to handle.

The two `.bat` files that ship are plain text - **open them in Notepad and read
every line**. The injector ships as `inject_autounlock.exe`, a PyInstaller build of
`inject_autounlock.py`, which is in this repository: so the thing to read is the
script, and because the build is reproducible you can rebuild the exe from it
and compare hashes instead of trusting the binary (see "The helper scripts"
below). Nothing is obfuscated and nothing is downloaded at run time: the exe is
a standard PyInstaller package of that script, and it rebuilds to the same
bytes.

---

## What's in here

| Path | What it is |
|---|---|
| `src/AutoUnlock.cpp` | The DLL source, 490 lines. |
| `src/build.bat` | Builds it with MSVC. |
| `helper-scripts/inject_main.bat` | Double-click this one. Asks for admin, injects, verifies. |
| `helper-scripts/_run_inject.bat` | Inner runner, called by the above. It runs the exe when present. |
| `helper-scripts/inject_autounlock.py` | The injector's source. The exe that ships is built from it. |
| `helper-scripts/build_injector.bat` | Builds `inject_autounlock.exe` from that source. |
| `src/items.txt` | The unlock list. 6612 entries, one per line, editable. |

`inject_autounlock.exe` itself is **not** in this repository: like the DLL it is
a build product, so it lives on the **Releases** page and in the release zip
instead. See "The helper scripts" below.

### Getting the files

Either take them from the **Releases** page of this repository, or build them
yourself: the DLL from `src/` (see [Building](#building)), the injector exe from
`helper-scripts/build_injector.bat`. Neither build product is committed, so a
plain checkout has neither.

`items.txt` sits in `src/` so that building there leaves `AutoUnlock.dll` and
`items.txt` side by side - that pair is the mod, and the DLL reads `items.txt`
from its own folder at runtime.

The release zip is flat: `AutoUnlock.dll` and `items.txt`, plus the three
helper files `inject_main.bat`, `_run_inject.bat` and `inject_autounlock.exe`,
in one folder. The injector's source and its build script stay in the
repository; the zip carries the finished exe.

**The injector only looks next to itself.** It resolves the DLL as
`<its own folder>/AutoUnlock.dll` and `items.txt` from the same place; it never
searches anywhere else. In a checkout that pair is split - the scripts live in
`helper-scripts/` while the DLL builds into `src/` - so running
`helper-scripts/inject_main.bat` from a checkout stops with `DLL not found`
(or, if the exe has not been built either, with "Neither injector nor python").
That is expected, not a bug.

To run it from a checkout: build the DLL (`src/build.bat`), build the exe
(`helper-scripts/build_injector.bat`) or have Python installed, then copy
`AutoUnlock.dll` and `items.txt` in beside the scripts. The "run build.bat
first" hint the script prints refers to the release layout, where they are
already together.

---

## Building

**Requirements**

- Windows 10 / 11
- **Build Tools for Visual Studio 2026** with the *Desktop development with C++*
  workload. It is free and includes no IDE:

  <https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2026>

  The installer is `vs_BuildTools.exe`. By default `vcvars64.bat` ends up in
  `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\`.

  Or via winget:

  ```
  winget install Microsoft.VisualStudio.BuildTools --force --override "--wait --passive --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64"
  ```

  Older toolsets (VS 2019 / 2022) work too - they just produce a different
  binary.

**Steps**

```
cd src
build.bat
```

`build.bat` calls `vcvars64.bat` and then:

```
cl /nologo /LD /O2 /MT /EHsc /W3 /Brepro /D_CRT_SECURE_NO_WARNINGS ^
   AutoUnlock.cpp /Fe:AutoUnlock.dll /Fo:obj\ ^
   /link kernel32.lib
```

`build.bat` locates the toolchain itself: it asks `vswhere.exe` first and falls
back to scanning the install roots. If it finds nothing it stops and tells you
what to install.

`/Brepro` makes the build reproducible, so the same source always produces the
same bytes on the same toolchain. Without it the linker stamps the build time
into the binary - twice, in the COFF header and again in the
`IMAGE_DEBUG_TYPE_REPRO` debug entry - and no two builds would ever match.

**Toolchain**

For reference, the released binary was built with:

```
Build Tools for Visual Studio 2026, version 18.7.4
Microsoft (R) C/C++ Optimizing Compiler Version 19.51.36248 for x64
Microsoft (R) Incremental Linker Version 14.51.36248.0
```

`build.bat` prints its own versions when you run it.

Any current MSVC builds this source. A different one links different library
code and so produces a different binary - expected, and harmless. What is worth
checking is the source, not the bytes.

---

## How it works

The full explanation is in the comment block at the top of
`src/AutoUnlock.cpp`. In short:

**1. Injecting.** The injector - `inject_autounlock.exe` in the release,
`inject_autounlock.py` in this repository, the same script - finds
`PVZBattleforNeighborville.exe`, opens it with administrator rights, writes the
path of `AutoUnlock.dll` into its memory, and starts a thread that calls
`LoadLibraryW`. That is the whole injection - about 200 lines of Python,
standard library only. PyInstaller is a build tool; none of it is in the logic.

**2. The console.** The DLL's `DllMain` starts a worker thread, which opens a
console window, opens `unlock_log.txt` and reads `items.txt`. Nothing is granted
yet. You type the number to start from.

**3. Granting.** For each entry the DLL builds the game's own
`DebugGrantItemsRequest`: an `Entry` object holding the unlock ID, wrapped in a
`TdfPrimitiveMap`, wrapped in a request object, dispatched into the game's Blaze
message pipeline. **The game does the granting**, which is why the items land in
your profile rather than being a memory illusion.

**4. The number you type** is the entry's position in `items.txt`, from 1 to
6612. Ctrl+C stops the run; the log prints the number to continue from, so a
disconnect does not cost you the whole list.

**5. Why it sometimes looks broken.** All three failure modes are silent:

- The Base Mod is not installed (see the top of this file).
- You are not in a map. The menu is not a map; granting there does nothing.
- You injected twice. Windows runs `DllMain` once per DLL path per process, so a
  second injection into the same game session does nothing. Close the game and
  start it again.

---

## The helper scripts

**No Python needed.** The release zip ships `inject_autounlock.exe`: the
injector with a Python interpreter packed inside it, so `inject_main.bat` works
on a machine that has no Python at all. `AutoUnlock.dll` itself is an ordinary
DLL, so any injector works as well - Cheat Engine, or whatever you already have.

**The exe is not in this repository.** Exactly like the DLL, it is a build
product: `.gitignore` keeps it out and it is published on the **Releases** page
instead. A git checkout therefore contains only the source -
`inject_autounlock.py` is what the exe is built from, not something you are
expected to run. The exe is the injector that ships.

Building it needs Python 3 (64-bit) and PyInstaller on the build machine; run
`helper-scripts/build_injector.bat` and it lands next to the script.
`_run_inject.bat` uses the exe when it is present and falls back to the script
otherwise - that fallback exists for development, and it is the only case where
Python is needed. <https://www.python.org/downloads/windows/>

The exe builds reproducibly on a fixed toolchain: `build_injector.bat` pins
`SOURCE_DATE_EPOCH` (the timestamp in the PE header) and `PYTHONHASHSEED`
(the iteration order PyInstaller walks its dictionaries in), and both are
needed - with only the first one, two builds still differ. Same Python and
PyInstaller versions plus the same paths give byte-identical files; a different
PyInstaller version still gives different bytes, so compare hashes only when
the versions match.

`inject_main.bat` asks for administrator rights - `OpenProcess` on the game
fails without them - and then runs the injector. The window closes by itself
when it succeeds; it only stays open if something failed, so you can read why.

`inject_autounlock.py` is the injector. It declares every Win32 function it
calls, so ctypes cannot silently truncate a 64-bit pointer, and it verifies the
injection by reading the log the DLL writes rather than assuming it worked.

`_run_inject.bat` is the small runner the first script calls. It is separate so
that the elevated window can run the injector and come back. It prefers
`inject_autounlock.exe` and only falls back to the Python script when the exe is
not there.

---

## License

MIT - see [LICENSE](LICENSE).

This project is not affiliated with, endorsed by, or sponsored by Electronic
Arts Inc. It is an independent, community-developed mod for educational and
preservation purposes. *Plants vs. Zombies* is a trademark of its respective
owners.
