"""Golden format/math cases and rejection tests for the offline GT7 replay converter."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

import numpy as np


MODULE = Path(__file__).resolve().parents[1] / "tools/dlss_probe/prepare_capture.py"
SPEC = importlib.util.spec_from_file_location("prepare_capture", MODULE)
prepare = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(prepare)


def packed(r, g, b):
    return r | (g << 11) | (b << 22)


class FormatTests(unittest.TestCase):
    def test_packed_channel_order_normal_subnormal_and_maximum(self):
        source = np.array([packed(15 << 6, 16 << 6, 14 << 5),
                           packed(1, 2, 1),
                           packed((30 << 6) | 63, (30 << 6) | 62, (30 << 5) | 31)], dtype="<u4")
        result = prepare.unpack_r11g11b10(source)
        np.testing.assert_array_equal(result[0], [1.0, 2.0, 0.5, 1.0])
        np.testing.assert_array_equal(result[1], [2.0 ** -20, 2.0 ** -19, 2.0 ** -19, 1.0])
        np.testing.assert_array_equal(result[2], [65024, 64512, 64512, 1])
        self.assertEqual(result.dtype, np.dtype("<f2"))

    def test_packed_infinity_and_nan_rejected_in_each_channel(self):
        for channel, shift, mantissa in ((0, 0, 6), (1, 11, 6), (2, 22, 5)):
            for payload in (0, 1):
                with self.subTest(channel=channel, payload=payload):
                    value = ((31 << mantissa) | payload) << shift
                    with self.assertRaisesRegex(prepare.CaptureError, "nonfinite"):
                        prepare.unpack_r11g11b10(np.array([value], dtype="<u4"))

    def test_motion_direction_y_axis_and_jitter_removal(self):
        motion = np.zeros((2, 4, 2), dtype="<f2")
        motion[:] = [-0.125, 0.25]  # Stationary geometry, current jitter only.
        motion[0, 1] = [0.375, -0.25]
        result, mask = prepare.convert_motion(motion, np.array([0.125, -0.25]), 4, 2)
        np.testing.assert_array_equal(result[0, 0], [0, 0])
        np.testing.assert_array_equal(result[0, 1], [-1, -0.5])
        self.assertFalse(mask.any())

    def test_invalid_sentinel_is_masked_without_becoming_claimed_valid_motion(self):
        motion = np.zeros((1, 3, 2), dtype="<f2")
        motion[0, 0] = [-1024, -1024]
        motion[0, 1] = [-1000, 0]  # Exact predicate is strictly less than -1000.
        result, mask = prepare.convert_motion(motion, np.array([0.0, 0.0]), 3, 1)
        np.testing.assert_array_equal(mask, [[255, 0, 0]])
        np.testing.assert_array_equal(result[0, 0], [0, 0])
        self.assertEqual(result[0, 1, 0], 1500)
        motion[0, 0, 1] = np.nan
        with self.assertRaisesRegex(prepare.CaptureError, "nonfinite"):
            prepare.convert_motion(motion, np.array([0.0, 0.0]), 3, 1)


class CaptureTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.capture = self.root / "capture"
        self.images = self.capture / "image-dumps"
        self.images.mkdir(parents=True)
        self.output = self.root / "output"
        self.constants_path = self.root / "constants.json"
        self.metadata = {}
        frames = []
        for capture in (1, 2):
            op = 100 + capture
            for slot in range(4):
                if slot == 0:
                    data = np.resize(np.array([0.0, 0.1, 0.5, 1.0], dtype="<f4"), (64, 64))
                elif slot == 1:
                    data = np.zeros((64, 64, 2), dtype="<f2")
                    data[:] = [-0.125, 0.25]
                    data[1, 1] = [-1024, -1024]
                elif slot == 2:
                    data = np.full((64, 64), packed(15 << 6, 16 << 6, 14 << 5), dtype="<u4")
                else:
                    data = np.full((64, 64, 4), 0.25, dtype="<f2")
                texel_bytes = data.nbytes // (64 * 64)
                stem = f"cap{capture}_op{op}_t{slot}"
                binary = self.images / (stem + ".bin")
                data.tofile(binary)
                metadata = {
                    "schema": "kyty.raw-image.v1", "complete": True, "byte_order": "little",
                    "capture": capture, "operation": op, "binary_file": binary.name,
                    "description": f"FG {op} ps tex[{slot}] read of {prepare.TAA_SHADER} 0x001200000000",
                    "payload_bytes": data.nbytes,
                    "host": {"width": 64, "height": 64, "depth": 1, "layers": 1, "mips": 1,
                             "vk_format": [130, 83, 122, 97][slot]},
                    "guest": {"width": 64, "height": 64, "depth": 1},
                    "selection": {"complete_selected_mip_aspect": True, "complete_texture": slot != 0,
                                  "mip": 0, "source_x_step": 1, "source_y_step": 1,
                                  "source_x_start": 0, "source_y_start": 0,
                                  "rows_subsampled": False, "tiles_truncated": False,
                                  "tile_start": 0, "tiles": 1, "available_tiles": 1,
                                  "source_width": 64, "source_height": 64, "rows": 64},
                    "storage": {"width": 64, "height": 64, "tiles": 1,
                                "aspect": "depth" if slot == 0 else "color", "texel_bytes": texel_bytes,
                                "padding_included": False, "order": "tile,row,column,texel byte",
                                "row_pitch": 64 * texel_bytes, "tile_pitch": data.nbytes}}
                path = self.images / (stem + ".json")
                self.metadata[capture, slot] = path
                self.write_json(path, metadata)
            values = [0.0] * 32
            values[14:22] = [32, -32, 64, 64, 1, 1, 0.125, -0.25]
            words = [f"{struct.unpack('<I', struct.pack('<f', v))[0]:08x}" for v in values]
            frames.append({"capture": capture, "op": op, "shader": prepare.TAA_SHADER,
                           "vertex_shader": prepare.VERTEX_SHADER,
                           "taa_constants": {"complete": True, "size_bytes": 128,
                                             "words_hex": words, "float_values": values},
                           "input_width": 64, "input_height": 64, "ndc_to_pixel": [32, -32],
                           "jitter_ndc": [0.125, -0.25], "motion_add_ndc": [0.125, -0.25],
                           "projection_jitter_pixels": [-4, -8],
                           "motion_to_backward_pixel_scale": [-32, 32]})
        self.constants = {"schema": 1, "source_capture": str(self.capture),
                          "source_shader_hashes": {"taa": prepare.TAA_SHADER}, "frames": frames}
        self.write_json(self.constants_path, self.constants)

    def tearDown(self):
        self.temp.cleanup()

    @staticmethod
    def write_json(path, value):
        path.write_text(json.dumps(value), encoding="utf-8")

    def mutate(self, capture, slot, change):
        path = self.metadata[capture, slot]
        meta = json.loads(path.read_text())
        change(meta)
        self.write_json(path, meta)

    def collect(self):
        return prepare.collect_inputs(self.capture, self.constants_path)

    def test_complete_native_sequence_and_source_hashes(self):
        sequence = prepare.prepare_capture(self.capture, self.output, self.constants_path)
        self.assertEqual(sequence["schema"], "gt7.ngx-replay.v1")
        self.assertEqual([f["capture"] for f in sequence["frames"]], [1, 2])
        frame = sequence["frames"][0]
        self.assertEqual(frame["invalid_motion_pixels"], 1)
        self.assertEqual(frame["jitter"], [-4, -8])
        self.assertEqual(frame["color"], "frame-0001.color.rgba16f")
        self.assertIn("not supplied to preset K", " ".join(sequence["limitations"]))
        self.assertEqual(len(frame["sources"]["0"]["sha256"]), 64)
        np.testing.assert_array_equal(np.fromfile(self.output / frame["color"], dtype="<f2").reshape(64, 64, 4)[0, 0],
                                      [1, 2, 0.5, 1])
        source_depth = self.images / "cap1_op101_t0.bin"
        self.assertEqual((self.output / frame["depth"]).read_bytes(), source_depth.read_bytes())
        np.testing.assert_array_equal(np.fromfile(self.output / frame["motion"], dtype="<f4"), np.zeros(64 * 64 * 2))
        mask = np.fromfile(self.output / frame["bias"], dtype=np.uint8).reshape(64, 64)
        self.assertEqual(mask[1, 1], 255)
        self.assertEqual(np.count_nonzero(mask), 1)

    def test_missing_input_rejected_before_output_creation(self):
        self.metadata[2, 3].unlink()
        with self.assertRaisesRegex(prepare.CaptureError, "missing TAA"):
            prepare.prepare_capture(self.capture, self.output, self.constants_path)
        self.assertFalse(self.output.exists())

    def test_subsampled_wrong_format_bad_size_and_unsafe_binary_rejected(self):
        path = self.metadata[1, 1]
        original = path.read_text()
        changes = [lambda m: m["selection"].update(source_y_step=2),
                   lambda m: m["selection"].update(complete_selected_mip_aspect=False),
                   lambda m: m["host"].update(vk_format=97),
                   lambda m: m.update(payload_bytes=1),
                   lambda m: m.update(binary_file="../outside.bin")]
        for change in changes:
            with self.subTest(change=change):
                path.write_text(original)
                self.mutate(1, 1, change)
                with self.assertRaises(prepare.CaptureError):
                    self.collect()

    def test_capture_gap_and_constant_mismatch_rejected(self):
        for slot in range(4):
            self.mutate(2, slot, lambda m: m.update(capture=3))
        with self.assertRaisesRegex(prepare.CaptureError, "consecutive"):
            self.collect()
        for slot in range(4):
            self.mutate(2, slot, lambda m: m.update(capture=2))
        self.constants["frames"][0]["op"] = 999
        self.write_json(self.constants_path, self.constants)
        with self.assertRaisesRegex(prepare.CaptureError, "operation"):
            self.collect()

    def test_constant_jitter_sign_mismatch_rejected(self):
        self.constants["frames"][0]["projection_jitter_pixels"] = [4, 8]
        self.write_json(self.constants_path, self.constants)
        with self.assertRaisesRegex(prepare.CaptureError, "jitter pixel sign"):
            self.collect()

    def test_replay_frame_count_and_dimension_contract(self):
        for slot in range(4):
            self.metadata[2, slot].unlink()
        with self.assertRaisesRegex(prepare.CaptureError, "2 to 8"):
            self.collect()
        self.mutate(1, 0, lambda m: m["host"].update(width=63))
        with self.assertRaisesRegex(prepare.CaptureError, "invalid dimensions"):
            self.collect()

    def test_nonfinite_depth_history_or_motion_never_publishes_sequence(self):
        for slot, dtype in ((0, "<f4"), (1, "<f2"), (3, "<f2")):
            with self.subTest(slot=slot):
                meta = json.loads(self.metadata[2, slot].read_text())
                path = self.images / meta["binary_file"]
                original = path.read_bytes()
                data = np.frombuffer(original, dtype=dtype).copy()
                data[0] = np.nan
                data.tofile(path)
                out = self.root / f"nonfinite-{slot}"
                with self.assertRaisesRegex(prepare.CaptureError, "nonfinite"):
                    prepare.prepare_capture(self.capture, out, self.constants_path)
                self.assertFalse((out / "sequence.json").exists())
                path.write_bytes(original)

    def test_existing_output_not_overwritten(self):
        self.output.mkdir()
        marker = self.output / "keep.bin"
        marker.write_bytes(b"keep")
        with self.assertRaisesRegex(prepare.CaptureError, "new or empty"):
            prepare.prepare_capture(self.capture, self.output, self.constants_path)
        self.assertEqual(marker.read_bytes(), b"keep")


if __name__ == "__main__":
    unittest.main()
