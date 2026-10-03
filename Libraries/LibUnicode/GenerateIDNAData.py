#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Generate immutable Unicode 17.0 UTS #46 / IDNA tables for LibUnicode.

Regeneration uses the pinned official UCD-17.0.0.zip and IDNA-17.0.0.zip
archives. The target does not depend on Python or on host ICU; it consumes the
generated C++ snapshot.
"""

from __future__ import annotations

import pathlib
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent
OUTPUT = ROOT / "IDNAData.h"
UCD_ARCHIVE = ROOT / "IDNAData" / "UCD-17.0.0.zip"
IDNA_ARCHIVE = ROOT / "IDNAData" / "IDNA-17.0.0.zip"
EXPECTED_UNICODE_VERSION = "17.0.0"


def cpp_u32(value: int) -> str:
    return f"0x{value:08X}u"


def emit_array(lines: list[str], type_name: str, name: str, rows: list[str]) -> None:
    lines.append(f"inline constexpr std::array<{type_name}, {len(rows)}> {name} {{{{")
    lines.extend(f"    {row}," for row in rows)
    lines.append("}};")
    lines.append("")


def compress_value_ranges(values: bytearray) -> list[tuple[int, int, int]]:
    if not values:
        return []
    result: list[tuple[int, int, int]] = []
    start = 0
    previous = values[0]
    for code_point, value in enumerate(values[1:], 1):
        if value != previous:
            result.append((start, code_point, previous))
            start = code_point
            previous = value
    result.append((start, len(values), previous))
    return result


def merge_ranges(ranges: list[tuple[int, int]]) -> list[tuple[int, int]]:
    merged: list[tuple[int, int]] = []
    for first, end in sorted(ranges):
        if merged and first <= merged[-1][1]:
            merged[-1] = (merged[-1][0], max(end, merged[-1][1]))
        else:
            merged.append((first, end))
    return merged


def parse_code_point_range(value: str) -> tuple[int, int]:
    if ".." in value:
        first, last = value.split("..")
        return int(first, 16), int(last, 16) + 1
    code_point = int(value, 16)
    return code_point, code_point + 1


def parse_uts46_mapping(contents: str) -> tuple[list[tuple[int, str, list[int]]], list[int]]:
    status_map = {
        "valid": "V",
        "mapped": "M",
        "ignored": "I",
        "deviation": "D",
        "disallowed": "X",
        "disallowed_STD3_valid": "3",
        "disallowed_STD3_mapped": "3",
    }
    header = contents.splitlines()[:12]
    if not header or not header[0].startswith("# IdnaMappingTable.txt"):
        raise SystemExit("not an official UTS #46 mapping table")
    if not any(f"# Version: {EXPECTED_UNICODE_VERSION}" in line for line in header):
        raise SystemExit("UTS #46 mapping data version mismatch")

    rows: list[tuple[int, str, list[int]]] = []
    mappings: list[int] = []
    expected_start = 0
    for line in contents.splitlines():
        data = line.split("#", 1)[0].strip()
        if not data:
            continue
        fields = [field.strip() for field in data.split(";")]
        first, end = parse_code_point_range(fields[0])
        if first != expected_start:
            raise SystemExit(f"UTS #46 mapping table has a gap or overlap at U+{first:04X}")
        expected_start = end
        status = status_map.get(fields[1])
        if status is None:
            raise SystemExit(f"unknown UTS #46 mapping status {fields[1]!r}")
        mapping = [int(value, 16) for value in fields[2].split()] if len(fields) > 2 and fields[2] else []
        rows.append((first, status, mapping))
        mappings.extend(mapping)

    if expected_start != 0x110000:
        raise SystemExit("UTS #46 mapping table does not cover all Unicode code points")
    return rows, mappings


def parse_idna_context_properties(contents: str) -> dict[str, list[tuple[int, int]]]:
    expected_header = f"# Idna2008-{EXPECTED_UNICODE_VERSION}.txt"
    if not contents.splitlines()[0].startswith(expected_header):
        raise SystemExit("IDNA derived-property data version mismatch")
    ranges = {"CONTEXTJ": [], "CONTEXTO": []}
    for line in contents.splitlines():
        data = line.split("#", 1)[0].strip()
        if not data:
            continue
        fields = [field.strip() for field in data.split(";")]
        if len(fields) >= 2 and fields[1] in ranges:
            ranges[fields[1]].append(parse_code_point_range(fields[0]))
    return {name: merge_ranges(values) for name, values in ranges.items()}


def parse_joining_types(contents: str) -> list[tuple[int, str]]:
    expected_header = f"# DerivedJoiningType-{EXPECTED_UNICODE_VERSION}.txt"
    if not contents.splitlines()[0].startswith(expected_header):
        raise SystemExit("derived joining-type data version mismatch")
    joining_types = []
    for line in contents.splitlines():
        data = line.split("#", 1)[0].strip()
        if not data:
            continue
        fields = [field.strip() for field in data.split(";")]
        if len(fields) < 2 or fields[1] not in ("L", "R", "D", "T"):
            continue
        first, end = parse_code_point_range(fields[0])
        joining_types.extend((code_point, fields[1]) for code_point in range(first, end))
    return sorted(joining_types)


def parse_script_ranges(contents: str, requested_scripts: tuple[str, ...]) -> dict[str, list[tuple[int, int]]]:
    expected_header = f"# Scripts-{EXPECTED_UNICODE_VERSION}.txt"
    if not contents.splitlines()[0].startswith(expected_header):
        raise SystemExit("script data version mismatch")
    ranges = {name: [] for name in requested_scripts}
    for line in contents.splitlines():
        data = line.split("#", 1)[0].strip()
        if not data:
            continue
        fields = [field.strip() for field in data.split(";")]
        if len(fields) >= 2 and fields[1] in ranges:
            ranges[fields[1]].append(parse_code_point_range(fields[0]))
    return {name: merge_ranges(values) for name, values in ranges.items()}


def parse_unicode_data(contents: str) -> tuple[list[tuple[int, list[int]]], list[tuple[int, int]], list[tuple[int, int]]]:
    decompositions: list[tuple[int, list[int]]] = []
    combining_classes: list[tuple[int, int]] = []
    mark_ranges: list[tuple[int, int]] = []
    pending_range: tuple[int, str] | None = None

    for line in contents.splitlines():
        fields = line.split(";")
        code_point = int(fields[0], 16)
        category = fields[2]
        combining_class = int(fields[3])
        decomposition = fields[5]
        name = fields[1]

        if name.endswith(", First>"):
            if pending_range is not None:
                raise SystemExit("malformed UnicodeData range start")
            pending_range = (code_point, category)
            continue
        if name.endswith(", Last>"):
            if pending_range is None or pending_range[1] != category:
                raise SystemExit("malformed UnicodeData range end")
            if category.startswith("M"):
                mark_ranges.append((pending_range[0], code_point + 1))
            pending_range = None
            continue

        if category.startswith("M"):
            mark_ranges.append((code_point, code_point + 1))
        if combining_class:
            combining_classes.append((code_point, combining_class))
        if decomposition and not decomposition.startswith("<"):
            decompositions.append((code_point, [int(value, 16) for value in decomposition.split()]))

    if pending_range is not None:
        raise SystemExit("unterminated UnicodeData range")
    return decompositions, combining_classes, mark_ranges


def parse_bidi_data(contents: str) -> bytearray:
    bidi_names = (
        "", "L", "R", "AL", "EN", "AN", "ES", "CS", "ET", "ON", "BN", "NSM",
        "B", "S", "WS", "LRE", "LRO", "RLE", "RLO", "PDF", "LRI", "RLI", "FSI", "PDI",
    )
    bidi_ids = {name: index for index, name in enumerate(bidi_names) if name}
    long_to_short = {
        "Left_To_Right": "L",
        "Right_To_Left": "R",
        "Arabic_Letter": "AL",
        "European_Number": "EN",
        "Arabic_Number": "AN",
        "European_Separator": "ES",
        "Common_Separator": "CS",
        "European_Terminator": "ET",
        "Other_Neutral": "ON",
        "Boundary_Neutral": "BN",
        "Nonspacing_Mark": "NSM",
        "Block_Separator": "B",
        "Segment_Separator": "S",
        "White_Space": "WS",
        "Left_To_Right_Embedding": "LRE",
        "Left_To_Right_Override": "LRO",
        "Right_To_Left_Embedding": "RLE",
        "Right_To_Left_Override": "RLO",
        "Pop_Directional_Format": "PDF",
        "Left_To_Right_Isolate": "LRI",
        "Right_To_Left_Isolate": "RLI",
        "First_Strong_Isolate": "FSI",
        "Pop_Directional_Isolate": "PDI",
    }
    long_to_short.update({bidi_name: bidi_name for bidi_name in bidi_ids})
    values = bytearray([bidi_ids["L"]]) * 0x110000

    def apply_range(property_text: str) -> None:
        range_text, bidi_name = (part.strip() for part in property_text.split(";", 1))
        first, end = parse_code_point_range(range_text)
        short_name = long_to_short.get(bidi_name)
        if short_name is None:
            raise SystemExit(f"unknown Unicode bidi class {bidi_name!r}")
        values[first:end] = bytes([bidi_ids[short_name]]) * (end - first)

    for line in contents.splitlines():
        if "@missing:" in line:
            apply_range(line.split("@missing:", 1)[1].strip())

    for line in contents.splitlines():
        data = line.split("#", 1)[0].strip()
        if data:
            apply_range(data)

    return values


def parse_full_composition_exclusions(contents: str) -> set[int]:
    exclusions: set[int] = set()
    for line in contents.splitlines():
        data = line.split("#", 1)[0].strip()
        if not data:
            continue
        code_point_range, property_name = (part.strip() for part in data.split(";", 1))
        if property_name != "Full_Composition_Exclusion":
            continue
        first, end = parse_code_point_range(code_point_range)
        exclusions.update(range(first, end))
    return exclusions


def main() -> None:
    if not UCD_ARCHIVE.is_file():
        raise SystemExit(f"missing pinned Unicode Character Database archive: {UCD_ARCHIVE}")
    if not IDNA_ARCHIVE.is_file():
        raise SystemExit(f"missing pinned IDNA data archive: {IDNA_ARCHIVE}")

    with zipfile.ZipFile(UCD_ARCHIVE) as archive:
        unicode_data = archive.read("UnicodeData.txt").decode("utf-8")
        bidi_data = archive.read("extracted/DerivedBidiClass.txt").decode("utf-8")
        normalization_data = archive.read("DerivedNormalizationProps.txt").decode("utf-8")
        joining_type_data = archive.read("extracted/DerivedJoiningType.txt").decode("utf-8")
        script_data = archive.read("Scripts.txt").decode("utf-8")
    with zipfile.ZipFile(IDNA_ARCHIVE) as archive:
        mapping_data = archive.read("IdnaMappingTable.txt").decode("utf-8")
        idna_property_data = archive.read("Idna2008.txt").decode("utf-8")
    for expected_header, contents in (
        (f"# DerivedBidiClass-{EXPECTED_UNICODE_VERSION}.txt", bidi_data),
        (f"# DerivedNormalizationProps-{EXPECTED_UNICODE_VERSION}.txt", normalization_data),
    ):
        if not contents.splitlines()[0].startswith(expected_header):
            raise SystemExit(f"Unicode data version mismatch: expected {expected_header}")

    uts46_rows, uts46_mappings = parse_uts46_mapping(mapping_data)
    idna_context_properties = parse_idna_context_properties(idna_property_data)
    joining_types = parse_joining_types(joining_type_data)
    script_ranges = parse_script_ranges(script_data, ("Greek", "Hebrew", "Han", "Hiragana", "Katakana"))

    lines = [
        "/* SPDX-License-Identifier: BSD-3-Clause AND Unicode-3.0 */",
        "/*",
        " * Copyright (c) 1991-2025, Unicode, Inc.",
        " * Generated by GenerateIDNAData.py. Do not edit by hand.",
        " * UTS #46 / IDNA derived data and Unicode normalization/bidi: Unicode 17.0.0.",
        " * Source data: Unicode Character Database and IDNA data files.",
        " * See IDNAData.LICENSE.md for third-party notices.",
        " */",
        "#pragma once",
        "#include <array>",
        "#include <cstdint>",
        "namespace Unicode::IDNA::Data {",
        "struct Uts46Row { std::uint32_t start; std::uint32_t mapping_offset; std::uint16_t mapping_length; char status; };",
        "struct Range { std::uint32_t first; std::uint32_t end; };",
        "struct JoiningType { std::uint32_t code_point; char value; };",
        "struct BidiRange { std::uint32_t first; std::uint32_t end; std::uint8_t value; };",
        "struct Decomposition { std::uint32_t code_point; std::uint32_t mapping_offset; std::uint8_t mapping_length; };",
        "struct CombiningClass { std::uint32_t code_point; std::uint8_t value; };",
        "struct Composition { std::uint32_t first; std::uint32_t second; std::uint32_t composed; };",
        "",
    ]

    uts_rows: list[str] = []
    mapping_offset = 0
    for start, status, mapping in uts46_rows:
        offset = mapping_offset
        mapping_offset += len(mapping)
        if len(status) != 1 or not status.isascii():
            raise SystemExit(f"invalid UTS #46 status {status!r}")
        uts_rows.append(
            f"{{ {cpp_u32(start)}, {cpp_u32(offset)}, {len(mapping)}u, '{status}' }}"
        )
    emit_array(lines, "Uts46Row", "uts46_rows", uts_rows)
    emit_array(lines, "std::uint32_t", "uts46_mappings", [cpp_u32(value) for value in uts46_mappings])

    for class_name in ("CONTEXTJ", "CONTEXTO"):
        rows = [f"{{ {cpp_u32(first)}, {cpp_u32(end)} }}"
                for first, end in idna_context_properties[class_name]]
        emit_array(lines, "Range", class_name.lower(), rows)

    joining_rows = [
        f"{{ {cpp_u32(code_point)}, '{joining_type}' }}"
        for code_point, joining_type in joining_types
    ]
    emit_array(lines, "JoiningType", "joining_types", joining_rows)

    for script_name in ("Greek", "Hebrew", "Han", "Hiragana", "Katakana"):
        rows = [f"{{ {cpp_u32(first)}, {cpp_u32(end)} }}"
                for first, end in script_ranges[script_name]]
        emit_array(lines, "Range", f"script_{script_name.lower()}", rows)

    bidi_values = parse_bidi_data(bidi_data)
    bidi_rows = [f"{{ {cpp_u32(first)}, {cpp_u32(end)}, {value}u }}"
                 for first, end, value in compress_value_ranges(bidi_values)]
    emit_array(lines, "BidiRange", "bidi_ranges", bidi_rows)

    source_decompositions, source_combining_classes, mark_ranges = parse_unicode_data(unicode_data)
    full_composition_exclusions = parse_full_composition_exclusions(normalization_data)
    decomposition_rows: list[str] = []
    decomposition_values: list[int] = []
    composition_rows: list[tuple[int, int, int]] = []
    for code_point, parts in source_decompositions:
        offset = len(decomposition_values)
        decomposition_values.extend(parts)
        decomposition_rows.append(
            f"{{ {cpp_u32(code_point)}, {cpp_u32(offset)}, {len(parts)}u }}"
        )
        if len(parts) == 2 and code_point not in full_composition_exclusions:
            composition_rows.append((parts[0], parts[1], code_point))

    mark_rows = [f"{{ {cpp_u32(first)}, {cpp_u32(end)} }}"
                 for first, end in merge_ranges(mark_ranges)]
    emit_array(lines, "Range", "mark_ranges", mark_rows)
    emit_array(lines, "Decomposition", "decompositions", decomposition_rows)
    emit_array(lines, "std::uint32_t", "decomposition_mappings",
               [cpp_u32(value) for value in decomposition_values])
    emit_array(lines, "CombiningClass", "combining_classes", [
        f"{{ {cpp_u32(code_point)}, {combining_class}u }}"
        for code_point, combining_class in source_combining_classes
    ])
    composition_rows.sort()
    emit_array(lines, "Composition", "compositions", [
        f"{{ {cpp_u32(first)}, {cpp_u32(second)}, {cpp_u32(composed)} }}"
        for first, second, composed in composition_rows
    ])

    lines.extend(["} // namespace Unicode::IDNA::Data", ""])
    OUTPUT.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    print(f"wrote {OUTPUT} ({OUTPUT.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
