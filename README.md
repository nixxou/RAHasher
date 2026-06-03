# RAHasher

RAHasher is a CLI utility for verifying ROM checksums [with hashing methods used by RetroAchievements](https://docs.retroachievements.org/developer-docs/game-identification.html).

_(It's a copy of the same utility provided by [RALibretro](https://github.com/RetroAchievements/RALibretro), but with a bit more convenient CLI.)_

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
$ git clone --recursive --depth 1 https://github.com/LeXofLeviafan/RAHasher.git
```

### Build

```
$ cd RAHasher
$ make -f Makefile.RAHasher HAVE_CHD=1
```

**NOTE**: use `make` for a release build or `make DEBUG=1` for a debug build. Don't forget to run `make clean` first if switching between a release build and a debug build.

## Building RALibretro with Visual Studio

### Clone the repo

```
> git clone --recursive --depth 1 https://github.com/LeXofLeviafan/RAHasher.git
```

### Build

Load `RALibretro.sln` in Visual Studio and build it (specifically the `RAHasher` target).

## Command Line Arguments

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
so large/solid `.7z` files are handled efficiently. You can combine it with `?` to
auto-detect the system of each entry by its name, e.g. `RAHasher.exe ? games.zip`.
Entries that are themselves archives are hashed as plain files (no recursion). The
only exception is the **Arcade** system, for which a `.zip` keeps being hashed as the
ROM itself (RetroAchievements hashes the archive as a whole).

### Archive options

These options go **before** the system key (like `-v`/`-s`):

Option|Description
-|-
`--arc-details`|Also print the entry's CRC32 and size: `<hash> <crc32> <size> <name>`. The CRC32 is taken from the archive (7z/zip/rar provide it; `00000000` if the format has none).
`--arc-calc-crc`|When the archive doesn't provide a CRC32 for an entry, compute it ourselves instead of printing `00000000`.
`--arc-ext list`|Only process entries with one of these extensions (comma-separated, case-insensitive; leading dot optional), e.g. `--arc-ext sfc,smc`. Others are ignored.
`--arc-filter pats`|Only process entries whose **name** matches one of the wildcard patterns.
`--arc-priority pats`|Process only **one** entry: the first match by priority order of the patterns (pattern 1 tried first, then pattern 2, …; ties broken by archive order).
`--arc-first`|Process only the **first** entry.

`--arc-filter`, `--arc-priority` and `--arc-first` operate on the subset left by
`--arc-ext`. Patterns are wildcards (`*` = any run, `?` = any char), matched against
the entry's file name **case-insensitively** and anchored to the whole name (use
`*USA*` for a substring). Separate multiple patterns with `,` — or with `,,` if a
pattern itself contains a comma (e.g. `--arc-priority "Zelda, The*,,Mario*"`).

Examples:
```bat
:: list crc + size of every .sfc/.smc, computing crc when missing
RAHasher.exe --arc-details --arc-calc-crc --arc-ext sfc,smc SNES "Secret of Mana.7z"

:: hash just the best regional variant present (USA preferred, then Europe, then Japan)
RAHasher.exe --arc-ext sfc --arc-priority "*(USA)*,*(Europe)*,*(Japan)*" SNES roms.zip
```

Archive support uses the 7-Zip library (`7z.dll`), located at runtime in this order:
`%RAHASHER_7Z_DLL%` → a `7z.dll` next to `RAHasher.exe` → the installed 7-Zip
(`HKLM\SOFTWARE\7-Zip`, then `C:\Program Files\7-Zip`). Drop the
[7-Zip-zstd](https://github.com/mcmilk/7-Zip-zstd/) build's `7z.dll` next to the
executable (or point `RAHASHER_7Z_DLL` at it) to also read Zstd-compressed archives.
