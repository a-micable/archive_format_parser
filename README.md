# Vector Archive

Vector Archive is a C++ command-line archiver and inspection toolkit for `.vec`
files. It can bundle files into one binary container, extract them again, list
archive metadata without extraction, and validate archive structure with
structured diagnostics.

## Format Overview

A `.vec` archive is parsed in stages:

1. Header: magic bytes (`VEC!`), format version, and archive flags.
2. Index table: entry count, per-entry chunk offsets, sizes, flags, and names.
3. Chunk stream: length-prefixed payload chunks, optionally marked as compressed
   or as nested `.vec` sub-archives.
4. Extension block: optional metadata that is read only after its checksum
   validates.

Integer fields use a compact variable-length encoding. File names are stored as
UTF-8 byte strings. Paths are normalized during packing so absolute paths and
parent directory components are not written into the archive.

## Build

```sh
cmake -S . -B build
cmake --build build
```

The CLI binary is written to `build/vector`.

To run the test suite:

```sh
ctest --test-dir build
```

## CLI Usage

Pack files into an archive:

```sh
vector pack file1.txt dir/file2.bin out.vec
```

Pack with simple run-length compression:

```sh
vector pack --compress file1.txt file2.txt out.vec
```

Unpack an archive into the current directory:

```sh
vector unpack out.vec
```

Unpack into a specific directory:

```sh
vector unpack out.vec extracted/
```

List an archive manifest without extracting files:

```sh
vector list out.vec
```

Produce JSON-like manifest output:

```sh
vector list --json out.vec
```

Verify archive layout and path policy:

```sh
vector verify out.vec
```

Produce JSON-like verification output:

```sh
vector verify --json out.vec
```

## Inspection And Validation

The inspection path parses the same header, index, chunk stream, and extension
sections used by extraction, but it records metadata instead of writing files.
Manifest entries include:

- Stored and normalized entry names.
- Original and packed sizes.
- Index flags and chunk flags.
- Relative and absolute chunk offsets.
- Payload offsets and encoded chunk end positions.
- Compression and nested archive state.
- Extension presence and checksum state.

The validation engine builds on the manifest and emits structured diagnostics.
Each issue has a severity, category, stable code, source range, optional entry
name, and optional detail. The CLI formatter prints concise text by default,
while the JSON-like formatter is intended for scripts and tests.

Validation currently checks:

- Header, index, chunk, and extension parse errors.
- Entry name policy, normalization changes, duplicates, and output collisions.
- Chunk bounds, gaps, overlaps, and index/chunk size agreement.
- Unknown flag bits and index/chunk flag disagreement.
- Compression ratio anomalies and entries whose stored size is surprising for
  their original size.
- Nested archive depth.
- Large archive and large entry policy limits.

## Library Modules

The project is organized into small first-party modules:

- `byte_io`: range-checked byte views, varint inspection, endian helpers, hex
  formatting, size formatting, and text helpers.
- `diagnostics`: structured issues, severity/category helpers, text formatting,
  and JSON-like serialization.
- `path_utils`: archive-name normalization, component classification, reserved
  name handling, collision resolution, and safe extraction paths.
- `format_rules`: format field descriptions, flag decoding, name and size
  classification, and format-level invariant checks.
- `manifest`: archive and entry metadata models plus text and JSON-like
  serializers.
- `inspect`: non-extracting parser that records chunk layout, extension state,
  and nested manifests.
- `validator`: validation rules for manifests and byte streams.
- `archive_stats`: size buckets, extension groups, depth summaries,
  compression summaries, path summaries, and archive layout segments.
- `report`: combined inspection, validation, and analysis reports for text or
  JSON-like output.

These modules are usable from tests or other C++ programs by linking
`vector_archive_lib`.

## Known Limitations

- Directories are not archived recursively; pass the files you want to include.
- File permissions, ownership, symlinks, and timestamps are not preserved.
- Compression is intentionally simple and is useful only for repetitive data.
- Existing files with the same names are overwritten during unpacking.
