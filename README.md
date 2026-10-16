# map-to-pdb

Convert an MSVC linker map file (`.map`) into a PDB that debuggers and disassemblers load automatically for the matching executable.

When a binary ships without symbols but its linker map is available, map-to-pdb turns every function and data label in the map into a symbol that WinDbg, Visual Studio, x64dbg, IDA, Ghidra and other PDB consumers understand.

```
> map-to-pdb game.map game.exe
image   game.exe (x64, 8 sections, timestamp 5557F2AE)
map     831024 symbols, 230529 functions (skipped 733 absolute and 0 discarded, merged 6275 repeated)
pdb     game.pdb
match   {FE3D543E-DE18-400A-A820-8F371BD688C1} age 3
```

## Features

- Single executable, no dependencies
- Matches the image's GUID and age, so debuggers load it without forcing
- Keeps public and static symbols, including statics that share a name
- Flags functions, so IDA creates them on load
- Checks the map against the image before writing anything
- Supports x86, x64 and ARM64, with maps from link.exe or lld-link
- Converts a 130 MB map in about 4 seconds

## Usage

```
map-to-pdb <file.map> <image> [options]
```

| Option | Description |
| --- | --- |
| `-o, --output <path>` | Output file. Default: the PDB name recorded in the image, placed next to the image. |
| `-f, --force` | Continue when the map does not match the image (timestamp or symbol addresses) or the image has no CodeView record. |
| `-y, --overwrite` | Replace the output file if it already exists. |
| `-h, --help` | Show help. |
| `-v, --version` | Show the version. |

Exit codes: `0` success, `1` error, `2` invalid command line.

The map must come from the same link as the image. Map files are produced by `link.exe /MAP` (or `lld-link /MAP`).

### When the image has no CodeView record

Images linked without `/DEBUG` have no CodeView record, so no PDB can be matched to them. With `--force`, map-to-pdb writes a PDB with a random GUID, which debuggers load only when told to ignore the mismatch (for example `.reload /i` in WinDbg).

## What ends up in the PDB

- Every map symbol becomes a public symbol (`S_PUB32`) under its decorated name. Symbols marked `f` in the map are flagged as functions, so disassemblers such as IDA create functions from them. Maps from `lld-link` have no function column; for those, symbols in executable sections are treated as functions.
- The image's section headers are copied in, so every `section:offset` address resolves.
- Absolute symbols and the entries newer linkers list at the image base for discarded sections are skipped.

### What a map file cannot provide

A map file contains names and addresses only. The PDB therefore has no types: no structures, class layouts, function prototypes, local variables or source lines. Class information that is encoded in names still comes through, such as member functions (`?Tick@CGame@@...`), vtables (`??_7CGame@@6B@`) and RTTI descriptors (`??_R0?AVCGame@@@8`), which RTTI-aware tools can use to rebuild class hierarchies.

## Building

Requirements: Visual Studio 2026 with the **Desktop development with C++** workload (v145 toolset, Windows 10 SDK).

Open `map-to-pdb.sln`, select **Production | x64** and build, or from a developer command prompt:

```
msbuild map-to-pdb.sln /p:Configuration=Production /p:Platform=x64
```

The executable is written to `bin\map-to-pdb.exe`.

The two configurations are **Development** and **Production**. Both keep runtime checks and standard library hardening enabled.

## How it works

1. **Image.** The PE headers give the machine, timestamp, section table and the CodeView (RSDS) record with the GUID, age and PDB name.
2. **Map.** Symbol lines from the "Publics by Value" and "Static symbols" tables are parsed, then sorted and de-duplicated.
3. **Validation.** Each symbol's `Rva+Base` must equal the image base plus its section's address plus its offset.
4. **PDB.** A PDB 7.0 file is written: an MSF container holding the PDB info stream (GUID, age), DBI stream (modules, section contributions, section map, section headers), an empty type stream, the public symbol hash table with its address map, and the symbol records.

The stream layout follows what `lld-link` emits. The symbol record stream and the public symbol address map are byte-for-byte identical to lld-link output for the same symbols. Hash table ordering matches the reference implementation, which readers rely on to stop scanning a bucket early.

## Testing

map-to-pdb has been verified against the two symbol engines used on Windows:

- **DIA** (`msdia140.dll`, used by Visual Studio and many disassemblers): `loadDataForExe` must accept the PDB as an exact match, and sampled symbols must resolve by address and by name with the expected number of matches.
- **DbgHelp** (used by WinDbg and many tools): the PDB must load with exact-match symbol options, and sampled symbols must resolve by name and by address.

The checks cover maps from x86, x64 and ARM64 images, a map with more than 800,000 symbols, names shared by statics in different object files, and names exactly 8 characters long.

## License

[MIT](LICENSE)
