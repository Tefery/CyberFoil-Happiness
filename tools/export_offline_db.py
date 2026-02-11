#!/usr/bin/env python3
"""
Convert CyberFoil-DB artefacts into runtime files used by CyberFoil offline mode.

Outputs:
  - titles.pack (binary metadata container)
  - icons.pack (single-file icon container)
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import shutil
import sqlite3
import struct
import sys
import re
from datetime import datetime, timezone
from typing import Dict, List, Tuple

TITLE_PACK_MAGIC = b"CFTITLE1"
TITLE_PACK_VERSION = 1
TITLE_PACK_ENTRY_SIZE = 48
TITLE_FLAG_HAS_NAME = 1 << 0
TITLE_FLAG_HAS_PUBLISHER = 1 << 1
TITLE_FLAG_HAS_INTRO = 1 << 2
TITLE_FLAG_HAS_DESCRIPTION = 1 << 3
TITLE_FLAG_HAS_SIZE = 1 << 4
TITLE_FLAG_HAS_VERSION = 1 << 5
TITLE_FLAG_HAS_RELEASE_DATE = 1 << 6
TITLE_FLAG_HAS_IS_DEMO = 1 << 7

ICON_PACK_MAGIC = b"CFICONP1"
ICON_PACK_VERSION = 1
ICON_PACK_ENTRY_SIZE = 32

ICON_DB_CANDIDATES = (
    "icon.db",
    "icons.db",
)

TITLES_JSON_CANDIDATES = (
    "titles.US.en.json",
    "titles.us.en.json",
    "title.US.en.json",
    "title.us.en.json",
    "titles.en.json",
    "titles.json",
)


def normalize_title_id(raw: str) -> str:
    value = raw.strip().lower()
    if value.startswith("0x"):
        value = value[2:]
    if len(value) < 16:
        value = value.rjust(16, "0")
    return value


def normalize_ext(raw_format: str) -> str:
    value = raw_format.strip().lower()
    if value in ("jpg", "jpeg"):
        return "jpg"
    if value in ("png", "webp", "bmp", "tif", "tiff"):
        return value
    return "bin"


def normalize_url_token(raw: str) -> str:
    # URL fields should not contain whitespace; strip all if present.
    value = raw.strip()
    if not value:
        return ""
    return re.sub(r"\s+", "", value)


def find_candidate_file(source_dir: pathlib.Path, candidates: Tuple[str, ...]) -> pathlib.Path | None:
    for name in candidates:
        path = source_dir / name
        if path.is_file():
            return path

    lower_candidates = {name.lower() for name in candidates}

    for path in source_dir.iterdir():
        if path.is_file() and path.name.lower() in lower_candidates:
            return path

    for path in sorted(source_dir.rglob("*")):
        if path.is_file() and path.name.lower() in lower_candidates:
            return path

    return None


def compute_file_sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as f:
        while True:
            chunk = f.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def build_manifest_file_entry(path: pathlib.Path, url: str) -> Dict[str, object]:
    return {
        "url": url,
        "size": path.stat().st_size,
        "sha256": compute_file_sha256(path),
    }


def export_metadata_pack(titles_json: pathlib.Path, output_path: pathlib.Path) -> int:
    with titles_json.open("r", encoding="utf-8") as f:
        raw = json.load(f)

    if not isinstance(raw, dict):
        raise RuntimeError(f"Expected top-level object in {titles_json}")

    rows: List[Tuple[int, str, str, str, str, int, int, int, int]] = []
    for key, value in raw.items():
        if not isinstance(value, dict):
            continue

        title_id = normalize_title_id(str(value.get("id") or key))
        title_int = int(title_id, 16)
        name = value.get("name")
        publisher = value.get("publisher")
        intro = value.get("intro")
        description = value.get("description")
        size = value.get("size")
        version = value.get("version")
        release_date = value.get("releaseDate")
        is_demo = value.get("isDemo")

        row = (
            title_int,
            name if isinstance(name, str) else "",
            publisher if isinstance(publisher, str) else "",
            intro if isinstance(intro, str) else "",
            description if isinstance(description, str) else "",
            int(size) if isinstance(size, int) and size >= 0 else -1,
            int(version) if isinstance(version, int) and version >= 0 else -1,
            int(release_date) if isinstance(release_date, int) and release_date >= 0 else -1,
            1 if is_demo is True else (0 if is_demo is False else -1),
        )

        # Keep only meaningful rows.
        if row[1] or row[2] or row[3] or row[4] or row[5] >= 0 or row[6] >= 0 or row[7] >= 0 or row[8] >= 0:
            rows.append(row)

    rows.sort(key=lambda x: x[0])

    output_path.parent.mkdir(parents=True, exist_ok=True)
    string_blob = bytearray(b"\0")
    string_offsets: Dict[str, int] = {"": 0}

    def intern_string(value: str) -> int:
        if not value:
            return 0
        offset = string_offsets.get(value)
        if offset is not None:
            return offset
        offset = len(string_blob)
        string_blob.extend(value.encode("utf-8", "ignore"))
        string_blob.append(0)
        string_offsets[value] = offset
        return offset

    with output_path.open("wb") as out:
        strings_offset = 32 + (len(rows) * TITLE_PACK_ENTRY_SIZE)
        out.write(
            struct.pack(
                "<8sIIIIQ",
                TITLE_PACK_MAGIC,
                TITLE_PACK_VERSION,
                TITLE_PACK_ENTRY_SIZE,
                len(rows),
                0,
                strings_offset,
            )
        )

        for row in rows:
            (
                title_int,
                name,
                publisher,
                intro,
                description,
                size,
                version,
                release_date,
                is_demo,
            ) = row

            flags = 0
            name_off = 0
            publisher_off = 0
            intro_off = 0
            description_off = 0

            if name:
                name_off = intern_string(name)
                flags |= TITLE_FLAG_HAS_NAME
            if publisher:
                publisher_off = intern_string(publisher)
                flags |= TITLE_FLAG_HAS_PUBLISHER
            if intro:
                intro_off = intern_string(intro)
                flags |= TITLE_FLAG_HAS_INTRO
            if description:
                description_off = intern_string(description)
                flags |= TITLE_FLAG_HAS_DESCRIPTION
            if size >= 0:
                flags |= TITLE_FLAG_HAS_SIZE
            if version >= 0:
                flags |= TITLE_FLAG_HAS_VERSION
            if release_date >= 0:
                flags |= TITLE_FLAG_HAS_RELEASE_DATE
            if is_demo >= 0:
                flags |= TITLE_FLAG_HAS_IS_DEMO

            out.write(
                struct.pack(
                    "<QIIIIQIIiI",
                    title_int,
                    name_off,
                    publisher_off,
                    intro_off,
                    description_off,
                    size if size >= 0 else 0,
                    version if version >= 0 else 0,
                    release_date if release_date >= 0 else 0,
                    is_demo,
                    flags,
                )
            )

        out.write(string_blob)

    return len(rows)


def export_icon_pack(icon_db: pathlib.Path, pack_path: pathlib.Path) -> int:
    pack_path.parent.mkdir(parents=True, exist_ok=True)
    temp_data = pack_path.with_suffix(pack_path.suffix + ".data.tmp")

    entries = []
    written = 0
    count = 0

    conn = sqlite3.connect(str(icon_db))
    try:
        cur = conn.cursor()
        cur.execute("SELECT title_id, format, image FROM images")
        with temp_data.open("wb") as data_out:
            while True:
                rows = cur.fetchmany(256)
                if not rows:
                    break
                for title_id, fmt, blob in rows:
                    if blob is None:
                        continue
                    payload = bytes(blob)
                    if not payload:
                        continue
                    title_hex = normalize_title_id(str(title_id))
                    title_int = int(title_hex, 16)
                    ext = normalize_ext(str(fmt))

                    entries.append((title_int, ext, written, len(payload)))
                    data_out.write(payload)
                    written += len(payload)

                    count += 1
                    if count % 1000 == 0:
                        print(f"[icons] packed {count}", flush=True)
    finally:
        conn.close()

    entries.sort(key=lambda x: x[0])

    data_offset = 32 + (len(entries) * ICON_PACK_ENTRY_SIZE)
    with pack_path.open("wb") as out:
        out.write(
            struct.pack(
                "<8sIIIIQ",
                ICON_PACK_MAGIC,
                ICON_PACK_VERSION,
                ICON_PACK_ENTRY_SIZE,
                len(entries),
                0,
                data_offset,
            )
        )

        for title_int, ext, offset, size in entries:
            ext_bytes = ext.encode("ascii", "ignore")[:7]
            ext_field = ext_bytes + (b"\0" * (8 - len(ext_bytes)))
            out.write(struct.pack("<QQI8sI", title_int, offset, size, ext_field, 0))

        with temp_data.open("rb") as data_in:
            shutil.copyfileobj(data_in, out, length=1024 * 1024)

    try:
        temp_data.unlink()
    except FileNotFoundError:
        pass

    return len(entries)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Export offline CyberFoil DB artefacts.")
    parser.add_argument(
        "--source-dir",
        type=pathlib.Path,
        help="Directory that contains original artefacts (icon.db and titles json).",
    )
    parser.add_argument("--icon-db", type=pathlib.Path, help="Path to icon.db (overrides discovery).")
    parser.add_argument("--titles-json", type=pathlib.Path, help="Path to titles json (overrides discovery).")
    parser.add_argument(
        "--output-dir",
        type=pathlib.Path,
        default=pathlib.Path("offline_db"),
        help="Output directory (default: ./offline_db).",
    )
    parser.add_argument("--skip-icons", action="store_true", help="Do not export icons")
    parser.add_argument("--skip-metadata", action="store_true", help="Do not export metadata")
    parser.add_argument(
        "--db-version",
        type=str,
        default="",
        help="Override manifest db_version (default: current UTC timestamp).",
    )
    parser.add_argument(
        "--manifest-base-url",
        type=str,
        default="",
        help="Base URL used to build manifest file URLs (example: https://github.com/<owner>/<repo>/releases/latest/download).",
    )
    parser.add_argument(
        "--manifest-name",
        type=str,
        default="offline_db_manifest.json",
        help="Manifest file name (default: offline_db_manifest.json).",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    need_icons = not args.skip_icons
    need_metadata = not args.skip_metadata
    if not need_icons and not need_metadata:
        print("Nothing to export (both --skip-icons and --skip-metadata set).")
        return 0

    icon_db = args.icon_db
    titles_json = args.titles_json
    if args.source_dir is not None:
        if not args.source_dir.is_dir():
            raise RuntimeError(f"source dir not found: {args.source_dir}")
        if need_icons and icon_db is None:
            icon_db = find_candidate_file(args.source_dir, ICON_DB_CANDIDATES)
        if need_metadata and titles_json is None:
            titles_json = find_candidate_file(args.source_dir, TITLES_JSON_CANDIDATES)

    if need_icons and icon_db is None:
        raise RuntimeError("Missing icon source. Provide --icon-db or --source-dir containing icon.db.")
    if need_metadata and titles_json is None:
        raise RuntimeError("Missing titles source. Provide --titles-json or --source-dir containing titles json.")

    if need_icons and not icon_db.is_file():
        raise RuntimeError(f"icon.db not found: {icon_db}")
    if need_metadata and not titles_json.is_file():
        raise RuntimeError(f"titles json not found: {titles_json}")

    output_dir: pathlib.Path = args.output_dir
    output_dir.mkdir(parents=True, exist_ok=True)
    generated_titles_pack: pathlib.Path | None = None
    generated_icons_pack: pathlib.Path | None = None

    if need_metadata:
        print(f"[metadata] source: {titles_json}")
        generated_titles_pack = output_dir / "titles.pack"
        metadata_count = export_metadata_pack(titles_json, generated_titles_pack)
        print(f"[metadata] exported {metadata_count} entries -> {generated_titles_pack}")

    if need_icons:
        print(f"[icons] source: {icon_db}")
        generated_icons_pack = output_dir / "icons.pack"
        icon_count = export_icon_pack(icon_db, generated_icons_pack)
        print(f"[icons] exported {icon_count} rows -> {generated_icons_pack}")

    if generated_titles_pack is not None and generated_icons_pack is not None:
        manifest_base_url = normalize_url_token(args.manifest_base_url)
        if manifest_base_url.endswith("/"):
            manifest_base_url = manifest_base_url[:-1]

        def file_url(file_name: str) -> str:
            if manifest_base_url:
                return f"{manifest_base_url}/{file_name}"
            return file_name

        db_version = args.db_version.strip()
        if not db_version:
            db_version = datetime.now(timezone.utc).strftime("%Y%m%d%H%M%S")

        manifest = {
            "schema": 1,
            "db_version": db_version,
            "generated_at_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
            "files": {
                "titles.pack": build_manifest_file_entry(generated_titles_pack, file_url("titles.pack")),
                "icons.pack": build_manifest_file_entry(generated_icons_pack, file_url("icons.pack")),
            },
        }

        manifest_path = output_dir / args.manifest_name
        with manifest_path.open("w", encoding="utf-8", newline="\n") as f:
            json.dump(manifest, f, ensure_ascii=False, indent=2, sort_keys=False)
            f.write("\n")
        print(f"[manifest] wrote {manifest_path}")
    else:
        print("[manifest] skipped (requires both metadata and icons outputs).")

    print("Done.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except Exception as exc:  # pragma: no cover
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
