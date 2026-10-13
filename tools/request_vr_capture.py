#!/usr/bin/env python3
"""Request native XR projection snapshots; these exclude compositor quad layers.

Example: python tools/request_vr_capture.py --request wrist-check --wait 30
The game must already be running a build that supports the request handler.
No game input, process launch, build flag or saved setting is changed.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import sys
import time
import uuid

LABEL = re.compile(r"[A-Za-z0-9_-]{1,48}\Z")
NONCE = re.compile(r"[0-9a-f]{32}\Z")
STAGES = ("source-left", "source-right", "destination-left", "destination-right")
MAX_PIXELS = 64 * 1024 * 1024


def validate_label(label: str) -> str:
    if not LABEL.fullmatch(label):
        raise ValueError("label must contain 1-48 letters, digits, underscores or hyphens")
    return label


def validate_id(nonce: str) -> str:
    if not NONCE.fullmatch(nonce):
        raise ValueError("capture ID must be 32 lowercase hexadecimal characters")
    return nonce


def request_capture(artifacts: Path, label: str) -> str:
    validate_label(label)
    artifacts.mkdir(parents=True, exist_ok=True)
    nonce = uuid.uuid4().hex
    request = artifacts / "capture-eye-pair.request"
    temporary = artifacts / f".capture-eye-pair-{nonce}.tmp"
    try:
        with temporary.open("x", encoding="ascii", newline="\n") as stream:
            stream.write(nonce + "\n")
            stream.flush()
            os.fsync(stream.fileno())
        # Publish a fully written request without replacing another caller's
        # pending request. Both names are on the same artifacts filesystem.
        os.link(temporary, request)
    finally:
        temporary.unlink(missing_ok=True)
    return nonce


def read_pair(artifacts: Path, nonce: str) -> list[tuple[Path, dict]] | None:
    validate_id(nonce)
    records: list[tuple[Path, dict]] = []
    for stage in STAGES:
        stem = artifacts / f"xr-request-{nonce}-{stage}"
        metadata_path = stem.with_suffix(".json")
        if not metadata_path.exists():
            return None
        if metadata_path.stat().st_size > 65536:
            raise ValueError(f"oversized metadata: {metadata_path.name}")
        try:
            metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError:
            # A producer may still be writing its final metadata record.
            return None
        if not isinstance(metadata, dict) or metadata.get("complete") is not True:
            return None
        if metadata.get("request_id") != nonce:
            raise ValueError(f"capture ID mismatch: {metadata_path.name}")
        if metadata.get("headset_compositor") is not False or metadata.get("runtime_submission_unverified") is not True:
            raise ValueError(f"missing projection-only evidence limits: {metadata_path.name}")
        width, height = metadata.get("width"), metadata.get("height")
        if type(width) is not int or type(height) is not int or not (0 < width <= 8192 and 0 < height <= 8192) or width * height > MAX_PIXELS:
            raise ValueError(f"invalid RGBA dimensions: {metadata_path.name}")
        for key in ("frame", "pose_epoch"):
            if type(metadata.get(key)) is not int or metadata[key] < (1 if key == "pose_epoch" else 0):
                raise ValueError(f"invalid {key}: {metadata_path.name}")
        raw = stem.with_suffix(".rgba")
        if not raw.is_file() or raw.stat().st_size != width * height * 4:
            raise ValueError(f"completed capture has missing/wrong-size RGBA: {raw.name}")
        records.append((raw, metadata))
    for key in ("frame", "pose_epoch"):
        if len({metadata[key] for _, metadata in records}) != 1:
            raise ValueError(f"capture images have mismatched {key}")
    return records


def convert_pair(records: list[tuple[Path, dict]]) -> list[Path]:
    from PIL import Image

    outputs = [raw.with_suffix(".png") for raw, _ in records]
    if any(path.exists() for path in outputs):
        raise FileExistsError("capture PNG already exists; preserving existing output")
    written: list[Path] = []
    try:
        for (raw, metadata), output in zip(records, outputs):
            pixels = raw.read_bytes()
            if len(pixels) != metadata["width"] * metadata["height"] * 4:
                raise ValueError(f"capture changed after validation: {raw.name}")
            with Image.frombytes("RGBA", (metadata["width"], metadata["height"]), pixels) as image:
                with output.open("xb") as stream:
                    written.append(output)
                    image.save(stream, format="PNG")
    except Exception:
        for output in written:
            output.unlink(missing_ok=True)
        raise
    return outputs


def wait_capture(artifacts: Path, nonce: str, seconds: float) -> list[Path] | None:
    if not 0 <= seconds <= 60:
        raise ValueError("wait must be between 0 and 60 seconds")
    deadline = time.monotonic() + seconds
    while True:
        records = read_pair(artifacts, nonce)
        if records is not None:
            return convert_pair(records)
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return None
        time.sleep(min(0.2, remaining))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--request", required=True, help="label for this request (letters/digits/_/-)")
    parser.add_argument("--wait", type=float, default=0, metavar="SECONDS", help="wait for four complete images, at most 60 seconds")
    args = parser.parse_args(argv)
    try:
        validate_label(args.request)
        if not 0 <= args.wait <= 60:
            raise ValueError("wait must be between 0 and 60 seconds")
        artifacts = Path(__file__).resolve().parent.parent / "artifacts"
        nonce = request_capture(artifacts, args.request)
        print(json.dumps({"request_id": nonce, "label": args.request, "headset_compositor": False,
                          "description": "XR projection images before compositor; wrist and other quad layers excluded"}), flush=True)
        if args.wait:
            outputs = wait_capture(artifacts, nonce, args.wait)
            if outputs is None:
                print("Timed out awaiting a fresh tracked projection pair. The tool did not cancel the request.", file=sys.stderr)
                return 2
            for output in outputs:
                print(str(output.resolve()))
        return 0
    except (OSError, ValueError, ImportError) as error:
        print(f"Capture request failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
