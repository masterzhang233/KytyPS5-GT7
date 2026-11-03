#!/usr/bin/env python3
"""Validate a GT7 TAA capture and prepare experimental native or 2x NGX replay inputs.

Requires NumPy. This is an offline format/convention conversion, not a DLSS game
integration, motion-vector validity repair, or a super-resolution benchmark.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import sys

import numpy as np


TAA_SHADER = "c3113b114fb02e5d"
VERTEX_SHADER = "f13bd2584bf580c0"
DESCRIPTION = re.compile(r"^FG (\d+) ps tex\[(\d+)\] read of ([0-9a-f]{16})\s")
EXPECTED = {0: (4, {(100, "color"), (126, "depth"), (130, "depth")}),
            1: (4, {(83, "color")}), 2: (4, {(122, "color")}),
            3: (8, {(97, "color")})}


class CaptureError(ValueError):
    """Input cannot safely be interpreted as the requested captured TAA pass."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise CaptureError(message)


def read_json(path: Path) -> dict:
    with path.open(encoding="utf-8-sig") as stream:
        value = json.load(stream)
    require(isinstance(value, dict), f"{path}: expected a JSON object")
    return value


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def finite(values: np.ndarray, label: str) -> None:
    require(bool(np.isfinite(values).all()), f"{label}: nonfinite values")


def vector(value: object, count: int, label: str) -> np.ndarray:
    require(isinstance(value, list) and len(value) == count, f"{label}: expected {count} numbers")
    try:
        result = np.asarray(value, dtype=np.float64)
    except (TypeError, ValueError) as exc:
        raise CaptureError(f"{label}: invalid numbers") from exc
    finite(result, label)
    return result


def close(actual: np.ndarray, expected: np.ndarray, label: str) -> None:
    require(bool(np.allclose(actual, expected, rtol=2e-7, atol=1e-10)),
            f"{label}: disagrees with captured constants ({actual} vs {expected})")


def extent(value: object, label: str) -> tuple[int, int]:
    require(isinstance(value, (list, tuple)) and len(value) == 2
            and all(type(n) is int and 64 <= n <= 4096 for n in value),
            f"{label}: expected two integer dimensions in 64..4096")
    return tuple(value)


def output_quality(input_extent: tuple[int, int], output_extent: tuple[int, int]) -> str:
    output_extent = extent(output_extent, "output extent")
    if output_extent == input_extent:
        return "dlaa"
    require(output_extent == tuple(2 * n for n in input_extent),
            "output ratio must be identical dimensions (DLAA) or exactly 2x on both axes (Performance)")
    return "performance"


def validate_schema2_frame(frame: dict, group: dict, values: np.ndarray,
                           width: int, height: int) -> tuple[int, int]:
    """Keep active render pixels distinct from a larger physical history backing."""
    active = (width, height)
    rect = frame["input_active_rectangle"]
    require(isinstance(rect, dict) and all(type(rect.get(k)) is int for k in ("x", "y", "width", "height"))
            and [rect["x"], rect["y"], rect["width"], rect["height"]] == [0, 0, width, height],
            "input active rectangle must start at (0,0) and equal the input extent")
    history_extent = (group[3]["width"], group[3]["height"])
    require(extent(frame["history_backing_extent"], "history backing extent") == history_extent,
            "history backing extent does not match the captured raw allocation")
    require(extent(frame["history_active_extent"], "history active extent") == active,
            "history active extent must equal the input extent")
    require(width <= history_extent[0] and height <= history_extent[1],
            "history active rectangle exceeds the captured backing")
    require(extent(frame["guest_taa_rt1_extent"], "guest TAA RT1 extent") == active,
            "guest TAA RT1 extent does not match input")
    close(vector(frame["history_coordinate_scale"], 2, "history scale"), values[18:20], "history scale cb72/76")
    close(values[18:20], np.ones(2), "supported history coordinate scale")
    close(vector(frame["history_coordinate_offset"], 2, "history offset"), values[24:26], "history offset cb96/100")
    close(values[24:26], np.zeros(2), "supported history coordinate offset")
    # The schema2 extraction carries the actual logged texture and RT descriptors,
    # so a physically larger history cannot silently substitute its size for W/H.
    textures = frame["textures"]
    require(isinstance(textures, list) and len(textures) == 4
            and sorted(t["slot"] for t in textures) == [0, 1, 2, 3],
            "schema2 requires exactly four texture descriptors")
    for texture in textures:
        raw = group[texture["slot"]]
        expected_format = [100, 83, 122, 97][texture["slot"]]
        require(texture["extent"] == [raw["width"], raw["height"], 1],
                "schema2 texture extent disagrees with raw metadata")
        require(int(texture["address"], 16) == raw["meta"]["guest"]["address"],
                "schema2 texture address disagrees with raw metadata")
        allowed_formats = (100, 126, 130) if texture["slot"] == 0 else (expected_format,)
        require(texture["format"] in allowed_formats, "schema2 texture format disagrees with input slot")
    targets = frame["render_targets"]
    require(isinstance(targets, list) and len(targets) == 2
            and sorted(t["slot"] for t in targets) == [0, 1],
            "schema2 requires both TAA render targets")
    for target in targets:
        expected_extent = history_extent if target["slot"] == 0 else active
        require(extent(target["extent"], "TAA target extent") == expected_extent
                and target["format"] == (97 if target["slot"] == 0 else 122),
                "schema2 TAA target extent/format mismatch")
    requested = extent([frame["output_width"], frame["output_height"]], "constants requested output extent")
    output_quality(active, requested)
    return requested


def unsigned_float_table(mantissa_bits: int) -> np.ndarray:
    """Vulkan unsigned mini-float: 5 exponent bits, bias 15, no sign bit."""
    bits = np.arange(1 << (mantissa_bits + 5), dtype=np.uint32)
    exponent = bits >> mantissa_bits
    mantissa = bits & ((1 << mantissa_bits) - 1)
    values = np.ldexp(1.0 + mantissa.astype(np.float32) / (1 << mantissa_bits),
                     exponent.astype(np.int32) - 15).astype(np.float32)
    subnormal = exponent == 0
    values[subnormal] = np.ldexp(mantissa[subnormal].astype(np.float32), -14 - mantissa_bits)
    values[(exponent == 31) & (mantissa == 0)] = np.inf
    values[(exponent == 31) & (mantissa != 0)] = np.nan
    return values


def unpack_r11g11b10(packed: np.ndarray) -> np.ndarray:
    """VK_FORMAT_B10G11R11_UFLOAT_PACK32: R=bits0..10, G=11..21, B=22..31."""
    source = np.asarray(packed, dtype="<u4")
    rg_table, b_table = unsigned_float_table(6), unsigned_float_table(5)
    output = np.empty(source.shape + (4,), dtype="<f2")
    for channel, shift, mask, table in ((0, 0, 0x7ff, rg_table),
                                        (1, 11, 0x7ff, rg_table),
                                        (2, 22, 0x3ff, b_table)):
        values = table[(source >> shift) & mask]
        finite(values, f"packed color channel {channel}")
        output[..., channel] = values
    output[..., 3] = 1.0
    finite(output, "converted RGBA16F color")
    return output


def convert_motion(motion: np.ndarray, jitter_ndc: np.ndarray,
                   width: int, height: int) -> tuple[np.ndarray, np.ndarray]:
    source = np.asarray(motion, dtype=np.float32)
    require(source.shape == (height, width, 2), "motion: incorrect dimensions")
    finite(source, "source motion")
    jitter = vector(list(jitter_ndc), 2, "jitter_ndc")
    invalid = source[..., 0] < -1000.0
    # Pixel coordinates have their origin at the top left. Raw M is current
    # jittered NDC minus previous unjittered NDC; add current J before reversing.
    output = (source + jitter.astype(np.float32)) * np.array(
        [-width / 2.0, height / 2.0], dtype=np.float32)
    output[invalid] = 0.0
    finite(output, "converted backward pixel motion")
    return np.asarray(output, dtype="<f4"), invalid.astype(np.uint8) * 255


def validate_raw(meta_path: Path, meta: dict, slot: int) -> dict:
    label = str(meta_path)
    require(meta.get("schema") == "kyty.raw-image.v1", f"{label}: unsupported raw schema")
    require(meta.get("complete") is True and meta.get("byte_order") == "little",
            f"{label}: incomplete or non-little-endian raw input")
    host, selection, storage, guest = (meta[k] for k in ("host", "selection", "storage", "guest"))
    width, height = host["width"], host["height"]
    require(type(width) is int and type(height) is int and 64 <= width <= 4096 and 64 <= height <= 4096,
            f"{label}: invalid dimensions")
    require(host["depth"] == 1 and host["layers"] == 1 and host["mips"] >= 1,
            f"{label}: only a single 2D layer is supported")
    require(selection.get("complete_selected_mip_aspect") is True and selection["mip"] == 0
            and selection["source_x_step"] == 1 and selection["source_y_step"] == 1
            and selection["source_x_start"] == 0 and selection["source_y_start"] == 0
            and selection.get("rows_subsampled") is False and selection.get("tiles_truncated") is False
            and selection["tile_start"] == 0 and selection["tiles"] == 1 and selection["available_tiles"] == 1,
            f"{label}: mip, tile, or row selection is incomplete/subsampled")
    require(selection["source_width"] == width and selection["source_height"] == height
            and selection["rows"] == height and storage["width"] == width
            and storage["height"] == height and storage["tiles"] == 1
            and guest["width"] == width and guest["height"] == height and guest["depth"] == 1,
            f"{label}: inconsistent image dimensions")
    texel_bytes, formats = EXPECTED[slot]
    require((host["vk_format"], storage["aspect"]) in formats and storage["texel_bytes"] == texel_bytes,
            f"{label}: wrong format/aspect for TAA slot {slot}")
    size = width * height * texel_bytes
    require(storage.get("padding_included") is False
            and storage["order"] == "tile,row,column,texel byte"
            and storage["row_pitch"] == width * texel_bytes and storage["tile_pitch"] == size
            and meta["payload_bytes"] == size,
            f"{label}: raw payload is not tightly packed or has the wrong length")
    filename = meta["binary_file"]
    require(isinstance(filename, str) and filename and Path(filename).name == filename
            and "/" not in filename and "\\" not in filename,
            f"{label}: binary filename must stay beside its metadata")
    binary = meta_path.parent / filename
    require(binary.is_file() and not binary.is_symlink() and binary.stat().st_size == size,
            f"{label}: missing binary or payload size mismatch")
    return {"metadata": meta_path, "binary": binary, "meta": meta,
            "width": width, "height": height, "slot": slot}


def collect_inputs(capture_dir: Path, constants_path: Path) -> tuple[dict, list[dict]]:
    constants = read_json(constants_path)
    require(type(constants.get("schema")) is int and constants["schema"] in (1, 2),
            "unsupported constants schema")
    schema2 = constants["schema"] == 2
    require(os.path.normcase(str(Path(constants["source_capture"]).resolve())) ==
            os.path.normcase(str(capture_dir.resolve())), "constants source_capture does not match capture directory")
    require(constants["source_shader_hashes"]["taa"] == TAA_SHADER, "constants use a different TAA shader")
    groups: dict[int, dict[int, dict]] = {}
    for path in sorted((capture_dir / "image-dumps").glob("*.json")):
        meta = read_json(path)
        match = DESCRIPTION.match(meta.get("description", ""))
        if not match or match[3] != TAA_SHADER:
            continue
        op, slot = int(match[1]), int(match[2])
        require(slot in EXPECTED, f"{path}: unexpected TAA input slot {slot}")
        capture = meta["capture"]
        require(type(capture) is int and capture > 0 and meta["operation"] == op,
                f"{path}: inconsistent capture/operation identity")
        group = groups.setdefault(capture, {})
        require(slot not in group, f"capture {capture}: duplicate TAA slot {slot}")
        group[slot] = validate_raw(path, meta, slot)
    require(bool(groups), "no captured TAA inputs found")
    captures = sorted(groups)
    require(2 <= len(captures) <= 8, "replay requires 2 to 8 captured frames")
    require(captures == list(range(captures[0], captures[-1] + 1)), "capture numbers are not consecutive")
    frames = constants["frames"]
    require(isinstance(frames, list) and len(frames) == len(captures), "constant/capture frame count mismatch")
    frame_map = {frame["capture"]: frame for frame in frames}
    require(len(frame_map) == len(frames) and sorted(frame_map) == captures, "constant/capture identifiers mismatch")
    prepared, dimensions, requested_output = [], None, None
    for capture in captures:
        group, frame = groups[capture], frame_map[capture]
        require(set(group) == set(EXPECTED), f"capture {capture}: missing TAA inputs (need slots 0,1,2,3)")
        width, height = group[0]["width"], group[0]["height"]
        equal_slots = (0, 1, 2) if schema2 else (0, 1, 2, 3)
        require(all((group[s]["width"], group[s]["height"]) == (width, height) for s in equal_slots),
                f"capture {capture}: input dimensions disagree")
        dimensions = dimensions or (width, height)
        require(dimensions == (width, height), "resolution changes across captures are unsupported")
        require(all(r["meta"]["operation"] == frame["op"] for r in group.values())
                and frame["shader"] == TAA_SHADER and frame["vertex_shader"] == VERTEX_SHADER,
                f"capture {capture}: constants do not match the captured shader/operation")
        cb = frame["taa_constants"]
        require(cb.get("complete") is True and cb["size_bytes"] == 128 and len(cb["words_hex"]) == 32,
                f"capture {capture}: incomplete TAA constants")
        require(all(isinstance(word, str) and re.fullmatch(r"[0-9a-fA-F]{8}", word)
                    for word in cb["words_hex"]), "invalid constant words")
        values = np.asarray(struct.unpack("<32f", b"".join(struct.pack("<I", int(w, 16))
                                                        for w in cb["words_hex"])), dtype=np.float64)
        finite(values, "TAA constants")
        close(vector(cb["float_values"], 32, "constant float values"), values, "constant words/values")
        require(frame["input_width"] == width and frame["input_height"] == height,
                f"capture {capture}: constant dimensions mismatch")
        close(values[16:18], np.array([width, height]), "constant image dimensions")
        d = np.array([width / 2.0, -height / 2.0])
        close(values[14:16], d, "constant NDC scale")
        scale_key = "ndc_to_input_pixel" if schema2 else "ndc_to_pixel"
        close(vector(frame[scale_key], 2, scale_key), d, "NDC scale")
        if schema2:
            requested = validate_schema2_frame(frame, group, values, width, height)
            requested_output = requested_output or requested
            require(requested_output == requested, "constants output extent changes across frames")
        j = vector(frame["jitter_ndc"], 2, "jitter_ndc")
        close(j, values[20:22], "jitter constant")
        close(vector(frame["motion_add_ndc"], 2, "motion_add_ndc"), j, "motion correction")
        close(vector(frame["motion_to_backward_pixel_scale"], 2, "motion scale"), -d, "motion scale")
        jitter_pixels = vector(frame["projection_jitter_pixels"], 2, "projection_jitter_pixels")
        close(jitter_pixels, -d * j, "projection jitter pixel sign/scale")
        prepared.append({"capture": capture, "op": frame["op"], "inputs": group,
                         "width": width, "height": height, "jitter_ndc": j,
                         "jitter_pixels": jitter_pixels,
                         "history_backing_extent": [group[3]["width"], group[3]["height"]],
                         "history_active_extent": [width, height],
                         "constants_requested_output": requested_output})
    return constants, prepared


def map_input(raw: dict, dtype: str, channels: int = 1) -> np.memmap:
    shape = (raw["height"], raw["width"]) + ((channels,) if channels != 1 else ())
    return np.memmap(raw["binary"], dtype=dtype, mode="r", shape=shape)


def prepare_capture(capture_dir: Path, output_dir: Path, constants_path: Path,
                    output_width: int | None = None, output_height: int | None = None) -> dict:
    capture_dir, output_dir, constants_path = (p.resolve() for p in (capture_dir, output_dir, constants_path))
    constants, frames = collect_inputs(capture_dir, constants_path)
    require((output_width is None) == (output_height is None),
            "specify both output width and output height, or neither")
    input_extent = (frames[0]["width"], frames[0]["height"])
    output_extent = input_extent if output_width is None else (output_width, output_height)
    quality = output_quality(input_extent, output_extent)
    require(not output_dir.exists() or (output_dir.is_dir() and not any(output_dir.iterdir())),
            "output directory must be new or empty; existing replay files will not be overwritten")
    output_dir.mkdir(parents=True, exist_ok=True)
    sequence = {
        "schema": "gt7.ngx-replay.v2", "width": input_extent[0], "height": input_extent[1],
        "output_width": output_extent[0], "output_height": output_extent[1], "quality": quality,
        "source_capture": str(capture_dir), "constants_source": str(constants_path),
        "constants_schema": constants["schema"], "constants_sha256": sha256(constants_path),
        "constants_requested_output": frames[0]["constants_requested_output"],
        "mode": "native-size experimental DLAA" if quality == "dlaa" else "2x experimental DLSS SR",
        "assumptions": ["Captured inputs retain their actual render dimensions; no CPU resize or downsampling.",
                        "Depth remains reverse-Z float32; no linearization or inversion.",
                        "Color retains captured HDR values without exposure or gamma conversion; alpha is 1.",
                        "Motion and jitter use active input-render pixels, never output or history-backing pixels.",
                        "Motion is current-to-previous pixel displacement after removing current projection jitter.",
                        "Frame time is assumed 16.6667 ms; actual frame timings were not captured.",
                        "NGX jitter uses the actual projection pixel shift (-width/2*J.x, +height/2*J.y)."],
        "limitations": ["This is offline DLAA/SR experimentation, not an in-game DLSS integration or FPS result.",
                        "Invalid motion (M.x < -1000) becomes numeric zero only as an untrusted placeholder.",
                        "bias.r8 is an independent invalid mask (255 invalid, 0 otherwise), not supplied to preset K.",
                        "Invalid regions are not quality validated; the mask does not repair motion or cause NGX rejection.",
                        "The game's entire previous history backing is validated; only its recorded active region is meaningful, and it is not supplied to NGX.",
                        "CLI output dimensions select the experiment; constants requested output dimensions describe the source extraction, not a guest output allocation.",
                        "Initial NGX history must be reset; exposure, disocclusion and per-material motion need visual validation."],
        "source_convention": constants.get("convention", {}), "frames": []}
    for frame in frames:
        width, height, group = frame["width"], frame["height"], frame["inputs"]
        depth = map_input(group[0], "<f4")
        motion_source = map_input(group[1], "<f2", 2)
        color_source = map_input(group[2], "<u4")
        history = map_input(group[3], "<f2", 4)
        finite(depth, "source depth")
        require(bool(((depth >= 0.0) & (depth <= 1.0)).all()), "reverse-Z depth outside [0,1]")
        finite(history, "source previous history")
        color = unpack_r11g11b10(color_source)
        motion, bias = convert_motion(motion_source, frame["jitter_ndc"], width, height)
        prefix = f"frame-{frame['capture']:04d}."
        names = {key: prefix + name for key, name in
                 {"color": "color.rgba16f", "depth": "depth.r32f", "motion": "motion.rg32f", "bias": "bias.r8"}.items()}
        for key, array in (("color", color), ("depth", depth), ("motion", motion), ("bias", bias)):
            array.tofile(output_dir / names[key])
        output_frame = {"capture": frame["capture"], "source_op": frame["op"],
                        **names,
                        "jitter": frame["jitter_pixels"].tolist(),
                        "input_active_rectangle": {"x": 0, "y": 0, "width": width, "height": height},
                        "history_backing_extent": frame["history_backing_extent"],
                        "history_active_extent": frame["history_active_extent"],
                        "invalid_motion_pixels": int(np.count_nonzero(bias)), "frame_time_ms": 16.6667,
                        "sources": {str(slot): {"metadata": str(raw["metadata"]),
                                                "metadata_sha256": sha256(raw["metadata"]),
                                                "binary": str(raw["binary"]), "sha256": sha256(raw["binary"])}
                                    for slot, raw in group.items()}}
        sequence["frames"].append(output_frame)
        del depth, motion_source, color_source, history, color, motion, bias
    # Only publish the loadable sequence once every frame passed validation and conversion.
    manifest = output_dir / "sequence.json"
    manifest.write_text(json.dumps(sequence, ensure_ascii=False, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    return sequence


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture_dir", type=Path)
    parser.add_argument("output_dir", type=Path)
    parser.add_argument("--constants", required=True, type=Path)
    parser.add_argument("--output-width", type=int)
    parser.add_argument("--output-height", type=int)
    args = parser.parse_args(argv)
    try:
        result = prepare_capture(args.capture_dir, args.output_dir, args.constants,
                                 args.output_width, args.output_height)
    except (CaptureError, OSError, KeyError, TypeError, ValueError) as exc:
        print(f"Capture preparation failed: {exc}", file=sys.stderr)
        return 1
    print(f"Prepared {len(result['frames'])} {result['width']}x{result['height']} -> "
          f"{result['output_width']}x{result['output_height']} {result['quality']} frames: "
          f"{args.output_dir.resolve() / 'sequence.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
