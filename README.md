# RAHasher

RAHasher is a CLI utility for verifying ROM checksums [with hashing methods used by RetroAchievements](https://docs.retroachievements.org/developer-docs/game-identification.html).

> **About this fork** — [`nixxou/RAHasher`](https://github.com/nixxou/RAHasher) is a
> fork of [`LeXofLeviafan/RAHasher`](https://github.com/LeXofLeviafan/RAHasher) (itself
> a CLI-friendly copy of [`RetroAchievements/RALibretro`](https://github.com/RetroAchievements/RALibretro)).
> It adds the ability to **read archives (`.zip`, `.rar`, `.7z`) and hash every ROM
> inside them in memory**, with options to filter/select entries and report each
> entry's CRC32 and size. See [Hashing archives](#hashing-archives-zip--rar--7z).

## What this fork adds

- **Archive hashing** — point RAHasher at a `.zip`/`.rar`/`.7z` file and it lists the
  RetroAchievements hash of every ROM inside it, one per line. Each entry is
  decompressed **into memory** one at a time (nothing is written to disk) via the
  7-Zip library, so large and *solid* `.7z` sets are handled efficiently.
- **Per-entry details** — optionally print each entry's **CRC32** (taken straight from
  the archive, or computed when the format has none) and its **64-bit size**.
- **Filtering / selection** — restrict by file extension, filter entry names by
  wildcard, or process a single entry (the first one, or the first match of a priority
  list).
- **Zstd archives** — drop the [7-Zip-zstd](https://github.com/mcmilk/7-Zip-zstd/)
  build of `7z.dll` next to the executable and Zstd-compressed archives work too, with
  no rebuild.

Everything else (the supported systems and the hashing algorithms themselves) is
unchanged from upstream.

## Building RAHasher with MSYS2/Makefile

### Install MSYS2

1. Go to [http://www.msys2.org/](http://www.msys2.org/) and download the 32 bit version.
2. Follow the installation instructions on the site [http://www.msys2.org/](http://www.msys2.org/).
3. When on the prompt for the first time, run `pacman -Syu` (**NOTE**: at the end of this command it will ask you to close the terminal window **without** going back to the prompt.)
4. Launch the MSYS2 terminal again and run `pacman -Su`

### Install the toolchain

```
$ pacman -S make git zip mingw-w64-i686-gcc mingw-w64-i686-SDL2 mingw-w64-i686-gcc-libs
```

### Clone the repo

```
$ git clone --recursive --depth 1 https://github.com/nixxou/RAHasher.git
```

### Build

```
$ cd RAHasher
$ make -f Makefile.RAHasher HAVE_CHD=1
```

**NOTE**: use `make` for a release build or `make DEBUG=1` for a debug build. Don't forget to run `make clean` first if switching between a release build and a debug build.

## Building RAHasher with Visual Studio

### Clone the repo

```
> git clone --recursive --depth 1 https://github.com/nixxou/RAHasher.git
```

### Build

Load `RALibretro.sln` in Visual Studio and build the `RAHasher` target (e.g.
`Release` / `x64`). The archive feature is Windows-only and links `oleaut32`
automatically; no extra setup is required at build time. At **run time** it needs a
`7z.dll` — see [Hashing archives](#hashing-archives-zip--rar--7z).

If you cloned without `--recursive`, fetch the submodules first:
```
> git submodule update --init --recursive
```

## Command Line Arguments

```
RAHasher.exe [-v] [-s systempath] [archive options] system filepath...
```

Argument|Description
-|-
-v|(optional) enables verbose messages for debugging
-s systempath|(optional) specifies where supplementary files are stored (typically a path to RetroArch/system)
system|specifies the system key or id associated to the game (which hash algorithm to use)
filepath|specifies the path to the game file (file may include wildcards, path may not)

I.e., in order to find out what checksum RA would assign to a NES game file, you can invoke the program like this:
```bat
RAHasher.exe NES "C:\ROMS\NES\Alwa's Awakening 8-bit edition.nes"
```
You can also pass `?` as the system key; in which case RAHasher will attempt to detect the system based on ROM file extension. (Note: equivalent "system ID" number is `91`.)

Additionally, you can pass multiple filenames and/or specify it a glob template (with `*`/`?` wildcards). Note that wildcards are only allowed in _filename itself_ (not in the path), and that system detection is not allowed in multiple files mode.

Finally, the full list of valid console keys/IDs will be printed along with usage info if you run RAHasher without arguments:
```bat
RAHasher.exe
```
The list ordering matches RetroAchievements website menu. (Also, system keys match short names from [RetroAchievents game lists](https://retroachievements.org/games), sans whitespace.)

## Hashing archives (zip / rar / 7z)

When `filepath` points to a `.zip`, `.rar` or `.7z` archive, RAHasher expands it and
prints the hash of **every file it contains**, one per line, as `<hash> <name>`:
```bat
RAHasher.exe MD "C:\ROMS\Genesis collection.7z"
```
```
cc6fb4e13300cbe82ad024d40c06a19a game1.md
e8d98d20cde7e224dce863078dc49e08 game2.md
```
Each entry is decompressed **into memory** one at a time (nothing is written to disk),
so large/solid `.7z` files are handled efficiently. Useful details:

- Combine it with `?` to auto-detect the system of each entry from its name, e.g.
  `RAHasher.exe ? games.zip`.
- Entries that are themselves archives are hashed as plain files (**no recursion**).
- The only exception is the **Arcade** system, for which a `.zip` keeps being hashed as
  the ROM itself (RetroAchievements hashes the archive as a whole).
- If an entry can't be hashed for the chosen system, its hash is shown as a row of `?`
  so the listing stays complete.

### Archive options

These options go **before** the system key (like `-v`/`-s`):

Option|Description
-|-
`--arc-details`|Also print the entry's CRC32 and size: `<hash> <crc32> <size> <name>`. The CRC32 is read from the archive (7z/zip/rar provide it; `00000000` if the format has none). The size is in bytes (64-bit).
`--arc-calc-crc`|When the archive doesn't provide a CRC32 for an entry, compute it ourselves instead of printing `00000000` (only meaningful together with `--arc-details`).
`--arc-ext list`|Only process entries with one of these extensions (comma-separated, case-insensitive; leading dot optional), e.g. `--arc-ext sfc,smc`. Others are ignored.
`--arc-filter pats`|Only process entries whose **name** matches one of the wildcard patterns.
`--arc-priority pats`|Process only **one** entry: the first match by priority order of the patterns (pattern 1 is tried first, then pattern 2, …; ties broken by archive order).
`--arc-first`|Process only the **first** entry.

`--arc-filter`, `--arc-priority` and `--arc-first` operate on the subset left by
`--arc-ext`. If both `--arc-priority` and `--arc-first` are given, priority wins.

Patterns are wildcards (`*` = any run of characters, `?` = any single character),
matched against the entry's file name **case-insensitively** and anchored to the whole
name — use `*USA*` to match a substring. Separate multiple patterns with `,`, or with
`,,` when a pattern itself contains a comma (e.g. names like `Zelda, The`):

```bat
RAHasher.exe --arc-priority "Zelda, The*,,Mario*" SNES roms.zip
```

#### Examples

```bat
:: list crc + size of every .sfc/.smc, computing crc when the format lacks it
RAHasher.exe --arc-details --arc-calc-crc --arc-ext sfc,smc SNES "Secret of Mana.7z"
```
```
10a894199a9adc50ff88815fd9853e19 d0176b24 2097152 Secret of Mana [USA][!!].sfc
...
```
```bat
:: hash just the best regional variant present (USA preferred, then Europe, then Japan)
RAHasher.exe --arc-ext sfc --arc-priority "*(USA)*,*(Europe)*,*(Japan)*" SNES roms.zip

:: hash only the first ROM in the archive
RAHasher.exe --arc-first SNES roms.7z
```

### The 7-Zip library (`7z.dll`)

Archive support uses the 7-Zip library at run time (no link-time dependency). The DLL
is located in this order:

1. `%RAHASHER_7Z_DLL%` — full path to a `7z.dll` of your choice
2. a `7z.dll` sitting next to `RAHasher.exe`
3. the installed 7-Zip from the registry (`HKLM\SOFTWARE\7-Zip`)
4. `C:\Program Files\7-Zip\7z.dll`, then `C:\Program Files (x86)\7-Zip\7z.dll`
5. `7z.dll` found on the system `PATH`

So installing [7-Zip](https://www.7-zip.org/) is enough. To also read **Zstd**-compressed
archives, use the [7-Zip-zstd](https://github.com/mcmilk/7-Zip-zstd/) build of `7z.dll`:
drop it next to `RAHasher.exe` or point `RAHASHER_7Z_DLL` at it — no rebuild needed,
since the interface is identical.
