#!/usr/bin/env python3
"""Regression tests using the literal FrameGraph log grammar from the renderer.

Fixtures are synthetic instances of real log formats, not captures from GT7.
They test conservative provenance and capture isolation, not shader semantics.
"""

import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "Analyze-GT7-TemporalInputs.py"
SPEC = importlib.util.spec_from_file_location("temporal_inputs", SCRIPT)
ANALYZER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ANALYZER)


def texture(op, slot, access, address, fmt=97, width=1280, height=720, flags="", view="0+1/0+1"):
    return (f"FG {op}   cs tex[{slot}] {access}{flags} 0x{address:012x}+0x100000 "
            f"{width}x{height}x1 layers 1 mips 1 view {view} fmt {fmt} guest 10 "
            "tile 1 type 2 [G--] overlaps 0 (0 GPU-written, 0x000000000000)\n")


def dispatch(op):
    return (f"FG {op} dispatch groups=160x90x1 mode=0x0 cs at 0x000200000000 queue gfx\n"
            f"FG {op}   cs 1234567890abcdef: 3 textures, 1 buffers, 8 user data, 0 bindless heaps\n")


def present(op, source=0x300000, resolved=0x300000, fmt=97, width=1280, height=720):
    return (f"FG {op} present source 0x{source:012x}+0x100000 {width}x{height} fmt {fmt} "
            f"resolved 0x{resolved:012x}+0x100000 {width}x{height} fmt {fmt} layer 0\n")


def capture(body, ops, number=1, frame=100, suffix=""):
    return (f"FG 0 begin capture {number} after frame end {frame}\n" + body
            + f"FG 0 end of frame: capture {number}, {ops} operations{suffix}\n"
            + f"FG 0 end capture {number} (2 shaders dumped)\n")


def analyze(text):
    return list(ANALYZER.iter_captures(io.StringIO(text)))


class TemporalInputsTests(unittest.TestCase):
    def test_structural_candidate_has_line_and_op_evidence(self):
        body = dispatch(1) + texture(1, 0, "write", 0x100000)
        body += dispatch(2) + texture(2, 0, "read", 0x100000)
        body += texture(2, 1, "read", 0x200000, fmt=83)  # RG is NOT proof of motion.
        body += texture(2, 2, "write", 0x300000, flags=" storage")
        body += "FG 2   cs user pointer s[0:1] 0x000800000000 [G]\n"
        body += "FG 2   cs user V# s[4:7] 0x000800001000+0x400 stride 16 records 64\n"
        body += "FG 2   cs buf[0] read 0x000800000000+0x100 (mapped 0x100) stride 16 records 16 fmt 0 [--] writers - hash 1234\n"
        body += "FG 2   cs user s0: 00000000 00000008\n"
        body += present(3)
        result, = analyze(capture(body, 3))
        self.assertTrue(result["complete"])
        self.assertFalse(result["dlss_ready"])
        self.assertEqual([(link["producer_op"], link["consumer_op"]) for link in result["texture_links"]], [(1, 2), (2, 3)])
        candidate, = result["temporal_candidates"]
        self.assertEqual(candidate["op"], 2)
        self.assertTrue(candidate["reaches_logged_present"])
        self.assertFalse(candidate["temporal_behavior_verified"])
        self.assertFalse(candidate["motion_vectors_verified"])
        self.assertEqual(len(candidate["constant_reference_lines"]), 2)
        op = result["operations"][1]
        self.assertEqual(op["buffer_bindings"][0]["mapped"], 256)
        self.assertEqual(op["constant_references"][1]["size_bytes"], 1024)
        self.assertEqual(op["shaders"][0]["hash"], "1234567890abcdef")
        self.assertEqual(len(op["user_data"]), 1)
        self.assertTrue(all(item["line"] > candidate["line"] for item in candidate["evidence"]))
        self.assertFalse(result["unresolved_reads"][0]["history_input_verified"])

    def test_present_matches_resolved_not_registered_source(self):
        body = dispatch(1) + texture(1, 0, "write", 0x100000)
        body += present(2, source=0x100000, resolved=0x300000)
        result, = analyze(capture(body, 2))
        self.assertEqual(result["texture_links"], [])
        output = result["operations"][1]["present"]
        self.assertEqual(output["source"]["address"], "0x000000100000")
        self.assertEqual(output["resolved"]["address"], "0x000000300000")
        self.assertFalse(output["pre_hud_color_verified"])

    def test_present_different_format_or_size_does_not_alias(self):
        for kwargs in ({"fmt": 37}, {"width": 1920}, {"height": 1080}):
            with self.subTest(**kwargs):
                result, = analyze(capture(dispatch(1) + texture(1, 0, "write", 0x300000)
                                          + present(2, **kwargs), 2))
                self.assertFalse(result["texture_links"])

    def test_overlapping_different_format_write_invalidates_old_producer(self):
        body = dispatch(1) + texture(1, 0, "write", 0x100000)
        body += dispatch(2) + texture(2, 0, "write", 0x180000, fmt=83)
        body += dispatch(3) + texture(3, 0, "read", 0x100000)
        result, = analyze(capture(body, 3))
        self.assertFalse(result["texture_links"])

    def test_multiple_views_and_found_backing_are_not_assumed_identical(self):
        body = dispatch(1) + texture(1, 0, "write", 0x100000)
        body += dispatch(2) + texture(2, 0, "read", 0x100000, view="0+1/0+2")
        body += texture(2, 1, "read", 0x100000)
        body += "FG 2   cs tex[1] found image 0x000000100000+0x100000 1920x1080x1 layers 1 mips 1 fmt 97, host 1920x1080\n"
        result, = analyze(capture(body, 2))
        self.assertFalse(result["texture_links"])
        self.assertTrue(all(r["reason"] == "ambiguous_view_or_backing" for r in result["unresolved_reads"]))

    def test_skipped_and_consumed_ops_are_excluded_but_bindings_retained(self):
        for skip in ("not run: empty arguments", "not drawn: no primitives",
                     "skipped: its pipeline is still compiling", "consumed as a clear: cs 1234567890abcdef"):
            with self.subTest(skip=skip):
                body = dispatch(1) + texture(1, 0, "read", 0x100000)
                body += texture(1, 1, "read", 0x200000) + texture(1, 2, "write", 0x300000)
                body += f"FG 1   {skip}\n" + present(2)
                result, = analyze(capture(body, 2))
                self.assertFalse(result["texture_links"])
                self.assertFalse(result["temporal_candidates"])
                self.assertEqual(len(result["operations"][0]["reads"]), 2)
                self.assertFalse(result["operations"][0]["eligible_for_graph"])

    def test_draw_rt_depth_and_disabled_color_write_mask(self):
        body = "FG 1 draw DrawIndexAuto count=3 instances=1\n"
        body += "FG 1   shaders: vertex 1111111111111111 ps 2222222222222222 topology 3\n"
        body += "FG 1   rt slot 0: 0x000000300000+0x100000 1280x720 mip 0 layer 0 fmt 97 guest 10 samples 1 tile 1 [G--] overlaps 0 (0 GPU-written)\n"
        body += "FG 1   rt slot 0 blend: enable 0 color 1 0 0 alpha 1 0 0 separate 0, write mask 0x0, bypass 0, clear word 0x00000000\n"
        body += "FG 1   depth: 0x000000100000+0x100000 1280x720 fmt 126 samples 1 test 1 write 1 op 6 bounds 0 stencil 0 clear 0 htile 0x000000000000 htile-clear 0\n"
        body += "FG 1   depth clear value 0, stencil clear 0 value 0\n"
        body += "FG 1   raster: clip disable 0 dx 1 z-clip off 0 0, vte 0x43f\n"
        body += present(2)
        result, = analyze(capture(body, 2))
        first = result["operations"][0]
        self.assertEqual(len(first["shaders"]), 2)
        self.assertEqual([r["role"] for r in first["writes"]], ["depth_attachment"])
        self.assertEqual(first["depth_attachments"][0]["compare_op"], 6)
        self.assertFalse(result["texture_links"])
        self.assertIn("depth_convention_and_camera_parameters", result["missing_requirements"])

    def test_consumed_clear_breaks_old_provenance_and_zero_dispatch_is_excluded(self):
        body = dispatch(1) + texture(1, 0, "write", 0x300000)
        body += dispatch(2) + "FG 2   consumed as a clear: cs 1234567890abcdef\n"
        body += present(3)
        result, = analyze(capture(body, 3))
        self.assertFalse(result["texture_links"])
        self.assertEqual(result["graph_gaps"][0]["op"], 2)
        body = dispatch(1).replace("groups=160x90x1", "groups=160x0x1")
        body += texture(1, 0, "write", 0x300000) + present(2)
        result, = analyze(capture(body, 2))
        self.assertFalse(result["operations"][0]["eligible_for_graph"])
        self.assertFalse(result["texture_links"])

    def test_truncated_and_missing_end_never_complete(self):
        cases = [capture(dispatch(1), 1, suffix=" (lines truncated)"),
                 capture(dispatch(1) + "FG 1 (the capture stops logging here: 200000 lines)\n", 1),
                 "FG 0 begin capture 1 after frame end 100\n" + dispatch(1),
                 "FG 0 begin capture 1 after frame end 100\n" + dispatch(1) + "FG 0 end capture 1 (0 shaders dumped)\n"]
        for text in cases:
            with self.subTest(text=text[-80:]):
                result, = analyze(text)
                self.assertFalse(result["complete"])
                self.assertFalse(result["dlss_ready"])

    def test_capture_sequence_and_id_mismatch_detected(self):
        for text in (capture(dispatch(2), 2),
                     capture(dispatch(1), 1).replace("end capture 1", "end capture 2"),
                     capture(dispatch(1), 1).replace("frame: capture 1", "frame: capture 2")):
            with self.subTest(text=text):
                result, = analyze(text)
                self.assertFalse(result["complete"])

    def test_memory_and_wait_beginops_preserve_numbering_and_break_stale_links(self):
        control_headers = [
            "dma 0x000000300080 <- 0x000000900000, 0x20 bytes, on the GPU, into an image's memory",
            "write data 0x000000300080 <- 0x00000001 0x00000002 (2 dwords) queue -1",
            "end of pipe 0x000000300080 <- 0x12 (8 bytes, source 2, event 0x14, interrupt 0) queue -1",
        ]
        for header in control_headers:
            with self.subTest(header=header):
                body = dispatch(1) + texture(1, 0, "write", 0x300000)
                body += "FG 2 wait 0x000100000010 passed: function 3 reference 0x1 mask 0xffffffff (4 bytes)\n"
                body += f"FG 3 {header}\n" + present(4)
                result, = analyze(capture(body, 4))
                self.assertTrue(result["complete"])
                self.assertEqual(result["operations"][1]["kind"], "wait")
                self.assertEqual(result["operations"][2]["header"], header)
                self.assertFalse(result["texture_links"])
                self.assertEqual(result["graph_gaps"][0]["op"], 3)

    def test_nonoverlapping_dma_and_gds_destination_do_not_destroy_image_link(self):
        for destination in ("0x000000900000", "0x000000300000 (gds)"):
            with self.subTest(destination=destination):
                body = dispatch(1) + texture(1, 0, "write", 0x300000)
                body += f"FG 2 dma {destination} <- 0x000000800000, 0x20 bytes, on the GPU\n"
                body += present(3)
                result, = analyze(capture(body, 3))
                self.assertTrue(result["complete"])
                self.assertEqual(len(result["texture_links"]), 1)
                self.assertFalse(result["graph_gaps"])

    def test_unknown_operation_prevents_complete_analysis_and_stale_provenance(self):
        body = dispatch(1) + texture(1, 0, "write", 0x300000)
        body += "FG 2 a future unrecognised command\n" + present(3)
        result, = analyze(capture(body, 3))
        self.assertFalse(result["complete"])
        self.assertFalse(result["texture_links"])
        self.assertIn("unrecognized_or_missing_operation_header", result["issues"])

    def test_capture_boundaries_prevent_history_and_op_reuse(self):
        text = capture(dispatch(1) + texture(1, 0, "write", 0x300000), 1)
        text += capture(present(1), 1, number=1, frame=101)  # New process may reuse capture ID.
        first, second = analyze(text)
        self.assertTrue(first["complete"] and second["complete"])
        self.assertFalse(second["texture_links"])
        self.assertEqual(second["sequence"], 2)
        text = "FG 0 begin capture 1 after frame end 100\n" + dispatch(1)
        text += capture(present(1), 1, number=2)
        first, second = analyze(text)
        self.assertFalse(first["complete"])
        self.assertIn("next_capture_before_end", first["issues"])
        self.assertTrue(second["complete"])

    def test_incidental_fg_quoted_data_and_post_frame_diagnostics_ignored(self):
        text = 'some text FG 0 begin capture 7 after frame end 1\n'
        text += '"FG 0 begin capture 7 after frame end 1"\n'
        self.assertEqual(analyze(text), [])
        text = capture(dispatch(1), 1).replace("FG 0 end capture", "FG 99   samples passed: 14\nFG 0 end capture")
        result, = analyze(text)
        self.assertTrue(result["complete"])
        self.assertEqual(len(result["operations"]), 1)

    def test_real_deferred_clear_completion_does_not_become_a_current_capture_op(self):
        # From the first real capture: an old conditional-clear callback arrives
        # after begin capture, before its actual first op (or before an op whose
        # number happens to match the callback). This is not a missing operation.
        for old_op in (2845, 1):
            body = (f"FG {old_op}   color clear check 26596 of 0x00123ad60000 layer 0 "
                    "(CMASK 0x00123b160000) landed: cleared it\n")
            body += present(1)
            body += ("FG 1   color clear check 26597 of 0x0002029e0000 layer 0 "
                     "(DCC 0x000206a30000) landed: nothing to clear\n")
            result, = analyze(capture(body, 1, number=3, frame=12551))
            self.assertTrue(result["complete"])
            self.assertEqual([op["op"] for op in result["operations"]], [1])
            self.assertEqual(len(result["deferred_diagnostics"]), 2)
            self.assertFalse(result["deferred_diagnostics"][0]["capture_attribution_verified"])

    def test_cli_streams_json_and_rejects_overwriting_log(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "capture.txt"
            output = Path(directory) / "analysis.json"
            log.write_text(capture(dispatch(1), 1), encoding="utf-8")
            command = [sys.executable, str(SCRIPT), "--log", str(log), "--output", str(output)]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            report = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(report["summary"], {"captures": 1, "complete_captures": 1, "incomplete_captures": 0})
            self.assertFalse(report["dlss_ready"])
            original = log.read_bytes()
            result = subprocess.run(command[:-1] + [str(log)], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(log.read_bytes(), original)
            log.write_text("No captures here\n", encoding="utf-8")
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(json.loads(output.read_text(encoding="utf-8"))["summary"]["captures"], 0)


if __name__ == "__main__":
    unittest.main()
