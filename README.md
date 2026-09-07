# vfs-verifier

**Acorn ADFS (Domesday) image tools**

## Overview

This repository holds two command line tools that work on Acorn ADFS filesystem
images recovered from Domesday LaserDiscs by the EFM decoding pipeline, together
with the bad sector map the decoder writes alongside each image.

- **vfs-verifier** checks one image against its bad sector map and reports any
  file whose data lands in a sector the decoder could not recover reliably. It is
  a *reporting* tool: it does not repair, extract or rewrite anything.
- **vfs-stacker** combines several images decoded from *different copies of the
  same disc* into one, taking each sector from a source whose bad sector map
  vouches for it. Where the copies are damaged in different places - which is
  usually the case - the stacked image is better than any of them alone, and can
  be good enough for vfs-verifier to pass. It reads the filesystem out of the
  merge to say whether the result is actually good, and which of the sectors it
  could not recover are ones anything depends on.

Both tools read the filesystem with the same code, so they always agree about
where it is, what it holds, and which sectors matter.

## Building

Building is Nix based; the flake pins nixpkgs, so no system packages need installing
beyond Nix itself (with flakes enabled).

```bash
# Build both binaries (they appear at ./result/bin/)
nix build

# Build and run one of them
nix run . -- <input.dat>                        # vfs-verifier
nix run .#vfs-stacker -- -o out.dat a.dat b.dat # vfs-stacker

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
./build/bin/vfs-stacker --help
```

The flake also exposes `nix flake check` (evaluates all outputs) and
`nix fmt` (formats the Nix files with `nixpkgs-fmt`).

Outside Nix the build needs CMake 3.16+, a C++17 compiler, and the `spdlog` and
`fmt` libraries.

### Source layout

```
src/common/     Code shared by both tools: logging, CLI parsing, the bad sector map,
                the ADFS parsers and the filesystem map that says which sectors
                the filesystem depends on
src/verifier/   vfs-verifier
src/stacker/    vfs-stacker
```

Each subdirectory of `src/` other than `common` becomes one executable, so adding
a tool means adding a directory.

---

# vfs-verifier

vfs-verifier locates the ADFS filesystem within the raw data-mode image, parses
the free space map and the root directory, and then reports any file whose data
lands in a sector that the decoder was unable to recover reliably.

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

---

# vfs-stacker

A single decode of a worn Domesday disc rarely comes back clean, but the damage
is particular to that copy: another pressing of the same disc, or another capture
of the same pressing, is usually bad in different places. vfs-stacker merges
several such decodes into one image by taking each EFM sector from a source whose
bad sector map vouches for it, and writes a new bad sector map listing only the
sectors that no source recovered.

Merging is a sector-level operation - if a sector is available from any source it
is used - but the report is not. The stacker reads the filesystem out of the
merge it has just planned and classifies every sector that is *still* bad against
the free space map and the root directory, so it can say whether the result is
actually good: which of the missing sectors carry file data, which hold
filesystem metadata, and which fall in free space or the run-in before the
filesystem and therefore cost nothing. A stack can be 98% recovered and perfect,
or 99.9% recovered and still missing a file - only the map can tell the
difference, and the run ends with a one-line verdict that says which it is.

## Usage

```bash
vfs-stacker -o <output> <input> <input> [<input>...]
```

### Arguments

- `inputs` - two or more VFS image files decoded from different copies of the
  same disc. As with the verifier, each bad sector map is read from the image
  path with `.bsm` appended, and a missing map is an error.

### Options

- `-h, --help` - Display help information
- `-o, --output <path>` - Output image; the merged bad sector map is written to `<path>.bsm`
- `--consensus <n>` - Also accept a sector that *every* source flagged as bad when at least `n` sources hold byte-identical content for it (0, the default, disables this; the minimum useful value is 2)
- `--dry-run` - Report what stacking would produce without writing anything
- `--force` - Stack the sources even when the alignment cross-check says they do not agree
- `--log-level <level>` - Console log level: `trace`, `debug`, `info` (default), `warn`, `error`, `critical`, `off`
- `--log-file <path>` - Also write a full debug-level log to `<path>` (the file is truncated on each run)

Per-sector decisions - which majority won, which sectors consensus rescued - are
logged at `debug` level.

### Example

```bash
# See whether three decodes of the same disc would give a good image, before
# committing 240 MB to disc. A dry run reaches exactly the same verdict as a
# real one; it simply does not write the result
vfs-stacker --dry-run ds2_efm.dat ds4_efm.dat ds6_efm.dat

# Do it, then confirm with the verifier
vfs-stacker -o national_a.dat ds2_efm.dat ds4_efm.dat ds6_efm.dat
vfs-verifier national_a.dat
```

The verdict at the end of the run is one of three:

- **GOOD** - every sector the filesystem depends on was recovered. Sectors may
  still be bad, but all of them are in free space, before the filesystem, or past
  the end of the image. vfs-verifier will pass the output.
- **INCOMPLETE** - sectors that the filesystem needs are bad in every source. The
  report names them, says which object each one falls in and how many bytes of it
  are lost, so it is clear what is missing and how much more damage matters.
  Another decode is needed to fill them.
- **UNKNOWN** - no ADFS filesystem could be read from the stacked image, so
  nothing can be said about whether the remaining damage matters. The merge is
  still written; the analysis is what is missing.

## What the stacker does

1. **Open the sources** - Each image and its bad sector map are opened and
   reported with its length, bad sector count and damage percentage. The sources
   need not be the same length; the output holds as many EFM sectors as the
   longest of them, and a shorter source simply has nothing to offer past its own
   end. Any trailing bytes beyond the last whole EFM sector are dropped.
2. **Alignment cross-check** - Every pair of sources is compared over the sectors
   both of them recovered. Decodes of the same disc must agree there, so
   widespread disagreement means the images are not aligned with each other -
   either they are decodes of different discs, or one of them gained or lost
   sectors during decoding, which displaces everything after it. Stacking
   misaligned images would splice unrelated data together, so the run stops
   unless `--force` is given. The agreement rate is reported for each pair.
3. **Plan the merge** - Where every output sector will come from is decided
   before anything is written, so that a dry run and a real run reach exactly the
   same conclusions. For each EFM sector of the output, the sources that hold it
   and do not list it as bad are the candidates:
   - **Agreement** - all the candidates hold the same bytes; that content is
     written. This is the normal case.
   - **Majority** - the candidates disagree, and the content held by more than
     half of them is written. Reported as a warning: the decoder vouched for
     sectors that cannot all be right.
   - **Split** - the candidates disagree with no majority. The content from the
     earliest source on the command line is written, and the sector is reported
     as a warning. Source order is the tie-break throughout, so putting the
     decode you trust most first makes it the arbiter.
   - **Consensus** (only with `--consensus <n>`) - no source vouches for the
     sector, but at least `n` of them hold byte-identical content for it, and
     that content is not just padding. Independent decoders do not make the same
     mistake twice, so the content is accepted despite the flags. Sectors that
     are entirely one fill byte (`0x00`, `0x20` or `0xFF`) are never rescued this
     way - agreement on padding says nothing about whether anything was
     recovered.
   - **Unrecovered** - none of the above. The sector is listed in the output bad
     sector map. The output is the same length whatever happens, so it is filled
     with the first source's copy that holds something other than padding, which
     keeps whatever partial data survived available for inspection. That content
     is flagged bad and must not be trusted.
4. **Read the filesystem** - The planned merge is parsed as an ADFS image without
   being written first: the `Hugo` signature is located and validated, and the
   free space map and root directory are read out of the merge exactly as
   vfs-verifier would read them from a file. This is the same code the verifier
   uses, so the two tools always agree about where the filesystem is and what it
   contains.
5. **Classify what is left** - Every sector that is still bad is put into one of
   six categories: within file data, filesystem metadata, allocated but belonging
   to no listed object, before the filesystem, free space, or past the end of the
   image. The first three are *vital* - something depends on them. The last three
   cost nothing. Vital sectors are listed as runs and named with the object they
   fall in, and each damaged object is reported with the number of bad EFM
   sectors, the bytes lost and the percentage left intact.
6. **Report and write** - The output is summarised by how each sector was arrived
   at, by what each source contributed, and against the best that any single
   source could manage alone, and the run ends with the GOOD / INCOMPLETE /
   UNKNOWN verdict. Unless `--dry-run` was given, the image and its bad sector
   map are written.

The contribution table measures each source against the filesystem:

- `Used` - sectors taken from that source.
- `OnlyGood` - sectors no other source recovered; what including it gained.
- `VitalOnly` - of those, the ones the filesystem actually depends on. This is
  the number that says whether a source earned its place: a decode that
  contributed thousands of free-space sectors and no vital ones added nothing.
- `VitalBad` - sectors the filesystem depends on that this source could not
  supply on its own, which is a fair measure of that decode's quality.

## Choosing sources

- The sources must be decodes of the **same disc**, and must be aligned with each
  other: byte offset *n* in one has to be byte offset *n* in the others. The
  decoder achieves this by placing each sector at its absolute address, so
  decodes from the same pipeline normally line up. The alignment cross-check will
  say if they do not.
- More sources is better, but with diminishing returns, and sources are not
  interchangeable: two decodes of the same physical copy share that copy's
  defects, whereas decodes of different copies do not.
- Damage common to every source cannot be stacked away. A run of sectors bad in
  all of them is either a defect shared by the pressing or a region the captures
  never reached, and no number of further copies of the same kind will fill it.

## Scope and limitations

- **Merging is still sector level.** The filesystem map decides what is
  *reported*, never what is merged: a sector available from any source is always
  used, whatever it holds. The stacker will not, for instance, prefer a source
  because its copy of a file looks more plausible.
- **The verdict is about damage the decoder detected.** GOOD means no sector the
  filesystem depends on is listed as bad, not that the file contents are correct.
  ADFS holds no per-file checksum, so nothing can confirm the bytes themselves;
  the verifier's fill and free-space-boundary checks look for the damage a bad
  sector map does not account for, and it is worth running it on the output.
- **Root directory only.** Like the verifier, the map covers the root directory;
  subdirectory contents show up as "allocated, not in any object", which is
  counted as vital but cannot be attributed to a named file.
- **The bad sector map is the authority.** Except with `--consensus`, a sector is
  taken from a source only when that source's map does not flag it. A sector the
  decoder wrongly believed it recovered is copied through as good, and one it
  wrongly condemned is not used. Undetected errors are beyond what a stacker can
  see.
- **The output is only as aligned as its inputs.** `--force` exists for cases
  where the operator knows better than the cross-check, and it removes the only
  protection against splicing unrelated data together.
- **Exit status does not reflect the verdict.** As with the verifier, the tool
  exits 0 whenever it completes, including on an INCOMPLETE or UNKNOWN result.
  A non-zero exit means the arguments were wrong, a file could not be read or
  written, or the sources failed the alignment cross-check without `--force`.
  Read the RESULT line for the outcome.

---

## Sector sizes

- ADFS logical sector: 256 bytes
- EFM sector: 2048 bytes (so one EFM sector covers eight ADFS sectors)

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

vfs-stacker writes its output map in the same format, in ascending order, so the
result can be fed straight back into either tool.

## License

GPLv3. See [LICENSE](LICENSE).
