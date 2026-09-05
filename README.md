# vfs-verifier

**Acorn ADFS (Domesday) image verifier**

## Overview

vfs-verifier checks an Acorn ADFS filesystem image recovered from a Domesday
LaserDisc against a bad sector map produced by the EFM decoding pipeline.

It locates the ADFS filesystem within the raw data-mode image, parses the free
space map and the root directory, and then reports any file whose data lands in
a sector that the decoder was unable to recover reliably. It is a *reporting*
tool: it does not repair, extract or rewrite anything.

## Building

Building is Nix based; the flake pins nixpkgs, so no system packages need installing
beyond Nix itself (with flakes enabled).

```bash
# Build the binary (appears at ./result/bin/vfs-verifier)
nix build

# Build and run in one step
nix run . -- <input.dat>

# Install into a profile
nix profile install .
```

### Development shell

```bash
# Drop into a shell with cmake, ninja, spdlog, fmt, gdb and clang-tools
nix develop

# Then the usual out-of-source CMake cycle
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
./build/bin/vfs-verifier --help
```

The flake also exposes `nix flake check` (evaluates all outputs) and
`nix fmt` (formats the Nix files with `nixpkgs-fmt`).

Outside Nix the build needs CMake 3.16+, a C++17 compiler, and the `spdlog` and
`fmt` libraries.

## Usage

```bash
vfs-verifier [options] <input>
```

### Arguments

- `input` - VFS image file: the binary output of efm-decoder in data mode

The bad sector map is found automatically: it is the input path with `.bsm`
appended, which is how the decoder names it (the suffix is added to the whole
filename rather than replacing the extension, so `disc.dat` pairs with
`disc.dat.bsm`). The run stops with an error if that file is not present.

### Options

- `-h, --help` - Display help information
- `--log-level <level>` - Console log level: `trace`, `debug`, `info` (default), `warn`, `error`, `critical`, `off`
- `--log-file <path>` - Also write a full debug-level log to `<path>` (the file is truncated on each run)

The console shows the full verification report at `info` level: filesystem
location, metadata integrity, image geometry, the directory listing, per-object
damage, the bad sector map analysis and the allocation cross-check. Problems are
raised at `warn` or `error` level. The `*FREE` and `*MAP` output, per-entry
details and the hex dumps of bad sectors are emitted at `debug` level, so use
`--log-level debug` or `--log-file` to see those.

### Example

```bash
# Decode to a data-mode image, then verify it
efm-decoder --mode data --output-metadata domesday.efm domesday.bin

# domesday.bin.bsm is picked up automatically
vfs-verifier domesday.bin \
  --log-level debug \
  --log-file domesday_verify.log
```

## What the verifier does

1. **Locate the filesystem** - The image is scanned for the ASCII directory
   identifier `Hugo`, which marks the start of the ADFS root directory. Each
   candidate is validated before it is accepted: the free space map sectors must
   checksum correctly and the root directory header and footer must agree. The
   first candidate that passes is used. If none pass, the first candidate found
   is used instead and the report says so, so that a badly damaged image can
   still be inspected.
2. **Free space map** - Logical sectors 0 and 1 are read with checksum
   verification and decoded into the free space map, disc name, disc ID, boot
   option and total sector count. The map is reported in the style of the Acorn
   `*FREE` and `*MAP` commands.
3. **Root directory** - The five sectors starting at logical sector 2 are parsed
   as an ADFS directory of up to 47 entries, terminated by a zero object-name
   byte. Each entry is listed with its object name, access flags (`DLRWErweP`),
   sequence number, load address, execution address, length and start sector.
   The directory is reported as broken if the master sequence number and `Hugo`
   identifier in the header do not match those in the footer.
4. **Metadata integrity** - The report states whether the free space map
   checksums pass, whether the root directory is consistent, and whether the EFM
   sectors holding that metadata are themselves listed in the bad sector map. If
   they are, everything derived from them is suspect and the report says so.
5. **Image geometry** - The number of sectors the free space map describes is
   compared with the number the image actually holds. A truncated image is
   reported, along with whether the missing sectors are free space or allocated
   (and so represent real data loss).
6. **Object damage** - For each directory entry the occupied 256-byte ADFS
   sectors are mapped to the 2048-byte EFM sectors that contain them and checked
   against the bad sector map. Affected objects are listed with the number of bad
   EFM sectors, the number of bytes lost and the percentage left intact. Each
   individual bad sector is named, with a hex dump of the affected ADFS sector,
   at `debug` level - on a badly damaged image there can be hundreds of these, so
   they are kept out of the summary report.
7. **Object content** - ADFS stores no checksum for file data, so content cannot
   be verified against anything. What is measured instead is whether an object
   holds any data at all: each 256-byte sector consisting entirely of one fill
   byte (`0x00`, `0x20` or `0xFF`) carries nothing. Objects are listed with their
   fill sector count, longest unbroken fill run, and how much of that fill the
   bad sector map does *not* account for. Fill the decoder never flagged is the
   interesting case - it believed it recovered those sectors, so the data is
   either blank on the disc or was lost without being detected. Fill is an
   observation, not a verdict: a blank region may be perfectly genuine.
8. **Free space boundary check** - Each free space extent is compared against
   the content actually present. If allocated data resumes later than the map
   declares, a run of sectors may have been lost or duplicated during decoding,
   which would displace every object after it. Residual data at the *start* of a
   free extent is reported separately and is normal - an erased object whose
   sectors were never overwritten.
9. **Bad sector map analysis** - Every entry in the bad sector map is classified:
   before the filesystem, within file data, filesystem metadata, allocated but
   belonging to no listed object, free space (harmless), or past the end of the
   image. This shows how much of the reported damage actually matters.
10. **Allocation cross-check** - The number of sectors the free space map marks as
   used is compared with the number the directory accounts for. A discrepancy
   indicates allocation slack or objects the listing does not reach.
11. **Summary** - A final line states whether any bad sectors affect file data.

### Sector sizes

- ADFS logical sector: 256 bytes
- EFM sector: 2048 bytes (so one EFM sector covers eight ADFS sectors)

## Scope and limitations

- **Root directory only.** Subdirectories are not descended into. Bad sectors in
  allocated space that no listed object accounts for are reported as their own
  category, so damage to a subdirectory's contents shows up as a count rather
  than being silently dropped.
- **No extraction.** Files are not written out; there is no output report file
  and no metadata export. All output goes to the console and, optionally, the
  log file.
- **File content cannot be verified.** ADFS stores no per-file checksum, and the
  directory checksum byte is zero (ignored by 8-bit ADFS), so there is nothing to
  check file bytes against. Checksum verification therefore applies only to the
  free space map sectors. Damage is derived from the bad sector map, and content
  analysis is limited to detecting sectors that carry no data at all.
- **Exit status does not reflect the verification result.** The tool exits 0
  whenever it completes, including when bad sectors were found. A non-zero exit
  means the arguments were wrong, or the image or bad sector map could not be
  opened or parsed. Check the summary log line for the verification outcome.
- **ADFS only.** There is no format selection; the image must be an Acorn ADFS
  filesystem with an 8-bit-style `Hugo` root directory.

## Bad sector map format

A plain text file with one decimal EFM sector number per line. Blank lines are
ignored, surrounding whitespace is trimmed, and lines that are not a plain
unsigned number are reported and skipped rather than aborting the run. Duplicate
entries are counted and reported once.

```
1043
1044
20871
```

## License

GPLv3. See [LICENSE](LICENSE).
