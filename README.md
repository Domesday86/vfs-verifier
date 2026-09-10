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
- `--consensus <n>` - Override the consensus rule, accepting a sector that *every* source flagged as bad when at least `n` sources hold byte-identical content for it (minimum 2, maximum the number of inputs). By default the requirement is a majority of the sources
- `--no-consensus` - Never accept a sector that every source flagged as bad, however many sources agree on its content
- `--no-pad` - Leave the output at the length the sources reached, instead of padding a short image out to the disc length the filesystem describes
- `--show-conflicts` - Hex dump the sectors the sources hold different content for, so that decode damage can be told from the sources being different versions of the disc
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
   - **Consensus** - no source vouches for the sector, but a majority of them
     hold byte-identical content for it, and that content is not just padding.
     Independent decoders do not arrive at the same 2048 bytes by accident, so
     the content is accepted despite the flags. This is on by default; see
     [Consensus recovery](#consensus-recovery) below. Sectors that are entirely
     one fill byte (`0x00`, `0x20` or `0xFF`) are never rescued this way -
     agreement on padding says nothing about whether anything was recovered.
   - **Unrecovered** - none of the above. The sector is listed in the output bad
     sector map. The output is the same length whatever happens, so it is filled
     with the content the most sources arrived at, ignoring any that hold nothing
     but padding. Even where no source will vouch for a sector, content several
     of them reached independently is a better guess than content only one of
     them holds. That content is flagged bad and must not be trusted.
4. **Read the filesystem** - The planned merge is parsed as an ADFS image without
   being written first: the `Hugo` signature is located and validated, and the
   free space map and root directory are read out of the merge exactly as
   vfs-verifier would read them from a file. This is the same code the verifier
   uses, so the two tools always agree about where the filesystem is and what it
   contains.
5. **Pad to the disc length** - The free space map says how many sectors the disc
   holds, which is not always how many the captures reached. If the merge is
   short, the tail is added as empty sectors so that the output is the length the
   filesystem describes. See [Padding a short capture](#padding-a-short-capture).
6. **Classify what is left** - Every sector that is still bad is put into one of
   six categories: within file data, filesystem metadata, allocated but belonging
   to no listed object, before the filesystem, free space, or past the end of the
   image. The first three are *vital* - something depends on them. The last three
   cost nothing. Vital sectors are listed as runs and named with the object they
   fall in, and each damaged object is reported with the number of bad EFM
   sectors, the bytes lost and the percentage left intact.
7. **Report and write** - The output is summarised by how each sector was arrived
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

### Consensus recovery

A sector every source flagged as bad cannot be recovered by picking a source that
vouches for it, because none does. But the sources still hold *content* for it,
and a decoder that fails on a sector does not fail in the same way twice: two
independent decodes landing on the same 2048 bytes is not a coincidence, it is
the disc. So the stacker accepts such a sector when a **majority** of the sources
that hold it are byte-identical, and that content is not just padding.

This is on by default because the alternative is throwing away recovery the
sources have already paid for. It is also the one place where the stacker
overrules a decoder, so every recovery is accounted for in the report:

- `Every source agreed` - not one source dissented. Beyond argument.
- `Accepted on two sources` - the weakest evidence the stacker will act on, and
  reported as a warning with the sectors named. With only two sources this is
  every consensus recovery there is.
- `Contested` - two or more sources held a *different* answer. That is not decode
  noise, which is random; it is the sources disagreeing about what the disc says,
  and it is worth confirming they really are the same disc.

Use `--consensus <n>` to demand a fixed number of agreeing sources instead of a
majority, or `--no-consensus` to switch it off and take the bad sector maps
literally. When sectors the filesystem needs are still bad but do have agreement
behind them, the INCOMPLETE verdict says how many and what threshold would take
them, so the choice is an informed one rather than a guess.

The independence this rests on cannot be verified, only estimated. Two sources
whose bad sector maps are *identical* are almost certainly the same decode rather
than two attempts, and the cross-check names them: such a pair adds nothing to
the stack and makes sectors look better agreed-upon than they are.

### Damage, or a different version of the disc?

Two quite different things make sources disagree, and they need opposite
responses. A bad decode is worth stacking away. Two *versions* of the same title
must not be stacked at all - merging them splices unrelated content together and
the result is a disc that never existed. The stacker reports every sector the
sources held different content for, and tries to say which of the two it is
looking at.

Three signals separate them:

- **Did anyone vouch for it?** A decoder that flags a sector good is asserting it
  read the disc correctly. Two sources vouching for the same sector and still
  disagreeing means the discs differ. Where *nothing* vouched for a sector, both
  sides are guesses and the difference is much more likely to be damage.
- **Do the same sources always take the same side?** Decode damage is random -
  which source dissents varies sector to sector. A different pressing is
  systematic: the same sources dissent every time, because they really are a
  different disc. The report groups the conflicts by exactly this split.
- **Is one side padding?** Where one source holds `0x00`/`0x20`/`0xFF` and the
  other holds data, they are not disagreeing about the disc - one of them simply
  recovered more of it. The report counts these separately from real differences.

`--show-conflicts` then hex dumps the sectors themselves, one row per distinct
content, showing only the rows that differ with `^^` under the differing bytes:

```
  EFM sector 100000 at 0xC350000 - no majority; first source taken,
                                   1736 byte(s) differ in 184 run(s)
    A - 1 source(s), chosen: version_a.dat (good)
    B - 1 source(s): version_b.dat (good)
      0x0000  A  67 20 74 6f 20 67 6f 20 75 70 2e 20 54 68 65 20 |g to go up. The |
      0x0000  B  69 63 68 20 74 68 65 20 20 20 20 20 20 20 20 20 |ich the         |
                 ^^ ^^ ^^ ^^ ^^ ^^ ^^    ^^ ^^ ^^    ^^ ^^ ^^
```

Two sources that both vouched for a sector, holding readable text that simply
says different things, is a version difference and no amount of stacking will
help.

Sources that fail the alignment cross-check are refused, but the same report is
produced anyway - being told *why* they were refused is the whole point when the
question is whether they are the same disc.

### Padding a short capture

A capture that stopped before the end of the disc produces an image shorter than
the disc it came from. That is not damage a bad sector map can express - those
sectors are not bad, they are absent - and it leaves the file the wrong length
even when everything in it is perfect. Discs like this are otherwise entirely
valid: the tail is usually free space, so nothing is actually lost.

The free space map states how many 256-byte sectors the disc holds, so the length
the image *should* be is known:

```
sector 0 position + (sectors on the disc x 256), rounded up to a whole EFM sector
```

When the merge falls short of that, the stacker appends empty sectors to make up
the difference. They are written as zeros and every one of them is listed in the
output bad sector map, so nothing can mistake padding for recovered data: the
role breakdown then says whether the missing tail was free space (harmless, and
the verdict stays GOOD) or something the filesystem depended on (vital, and the
verdict is INCOMPLETE). The report gives the captured length and the padded
length separately.

The length is only as trustworthy as the metadata it is read from, so padding is
skipped, with a warning saying so, unless:

- the `Hugo` signature passed full validation, **and**
- the free space map's checksums hold, **and**
- the free space map's own totals are self-consistent - free plus used has to
  equal the disc size it claims.

Padding never truncates. Sources that run *past* the declared end of the disc are
left as they are and reported, on the grounds that unexplained run-out is better
kept than thrown away. Use `--no-pad` to leave the output at whatever length the
captures reached.

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
  Consensus recovery is the one exception, and it only applies where the sources
  hold matching content rather than matching noise.
- Sources need not be the same length. The output takes the length of the longest
  of them, extended to the disc length if the filesystem says the disc is longer
  still, and a source that stopped early simply has nothing to offer past its
  end. The contribution table charges it for the tail it never reached.

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
- **The bad sector map is the authority.** Except in consensus recovery, a sector
  is taken from a source only when that source's map does not flag it. A sector
  the decoder wrongly believed it recovered is copied through as good, and one it
  wrongly condemned is only reconsidered when the other sources agree with it.
  Undetected errors are beyond what a stacker can see.
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
