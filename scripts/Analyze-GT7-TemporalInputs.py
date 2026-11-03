#!/usr/bin/env python3
"""Find evidence-backed *candidates* in Kyty FrameGraph logs; never certify DLSS inputs.

The log is read one line at a time and captures are analysed independently. Texture
links require identical guest ranges, dimensions, formats and single-subresource
views. They are observations of logged bindings, not proof of GPU execution or
pixel provenance. Unknown aliases deliberately break links rather than invent them.
"""

import argparse
import json
from pathlib import Path
import re
import sys


SCHEMA_VERSION = 1
MISSING = [
    "verified_motion_vectors_and_scale",
    "depth_convention_and_camera_parameters",
    "camera_jitter_and_history_reset",
    "correct_pre_hud_pre_temporal_insertion_stage",
    "exposure_and_color_space",
]
HEX = r"0x[0-9a-fA-F]+"
STAGE = r"(?:cs|ps|vs|ls|hs|tes|ms|\?)"
FG = re.compile(r"^FG (\d+) (.*)$")
ANSI = re.compile(r"\x1b\[[0-9;]*m")
BEGIN = re.compile(r"begin capture (\d+) after frame end (\d+)$")
FRAME_END = re.compile(r"end of frame: capture (\d+), (\d+) operations(.*)$")
END = re.compile(r"end capture (\d+) \((\d+) shaders dumped\)$")
# conditionalClear.cpp prints completion notes asynchronously with an old op ID,
# without a capture ID. They can arrive after the next capture has already begun.
DEFERRED_CLEAR = re.compile(
    rf"color clear check \d+ of {HEX} layer \d+ \((?:DCC|CMASK) {HEX}\) "
    r"landed: (?:cleared it|nothing to clear)$"
)
RESOURCE = rf"(?P<address>{HEX})\+0x(?P<size>[0-9a-fA-F]+) (?P<width>\d+)x(?P<height>\d+)"
TEXTURE = re.compile(
    rf"(?P<stage>{STAGE}) tex\[(?P<slot>\d+)\] (?P<access>read|write)"
    rf"(?P<flags>(?: bindless| runtime| storage)*) {RESOURCE}x(?P<depth>\d+)"
    r" layers (?P<layers>\d+) mips (?P<mips>\d+) view (?P<mip>\d+)\+(?P<mip_count>\d+)"
    r"/(?P<layer>\d+)\+(?P<layer_count>\d+) fmt (?P<format>\d+) guest (?P<guest_format>\d+)"
    r" tile (?P<tile>\d+) type (?P<type>\d+) \[(?P<state>[^\]]+)\](?: .*)?$"
)
FOUND_IMAGE = re.compile(
    rf"(?P<stage>{STAGE}) tex\[(?P<slot>\d+)\] found image {RESOURCE}x(?P<depth>\d+)"
    r" layers (?P<layers>\d+) mips (?P<mips>\d+) fmt (?P<format>\d+), host "
    r"(?P<host_width>\d+)x(?P<host_height>\d+)$"
)
RT = re.compile(
    rf"rt slot (?P<slot>\d+): {RESOURCE} mip (?P<mip>\d+) layer (?P<layer>\d+)"
    r" fmt (?P<format>\d+) guest (?P<guest_format>\d+) samples (?P<samples>\d+)"
    r" tile (?P<tile>\d+) \[(?P<state>[^\]]+)\](?: .*)?$"
)
DEPTH = re.compile(
    rf"depth: {RESOURCE} fmt (?P<format>\d+) samples (?P<samples>\d+)"
    r" test (?P<test>\d+) write (?P<write>\d+) op (?P<compare_op>\d+)"
    r" bounds (?P<bounds>\d+) stencil (?P<stencil>\d+) clear (?P<clear>\d+)(?: .*)?$"
)
BUFFER = re.compile(
    rf"(?P<stage>{STAGE}) buf\[(?P<slot>\d+)\] (?P<access>read|write)(?P<flags> formatted)?"
    rf" (?P<address>{HEX})\+0x(?P<size>[0-9a-fA-F]+) \(mapped 0x(?P<mapped>[0-9a-fA-F]+)\)"
    r" stride (?P<stride>\d+) records (?P<records>\d+) fmt (?P<format>\d+)(?: .*)?$"
)
POINTER = re.compile(
    rf"(?P<stage>{STAGE}) user (?P<kind>pointer|V#) s\[(?P<register_first>\d+):"
    rf"(?P<register_last>\d+)\] (?P<address>{HEX})(?:\+0x(?P<size>[0-9a-fA-F]+))?(?: .*)?$"
)
PRESENT = re.compile(
    rf"present source {RESOURCE} fmt (?P<format>\d+) resolved "
    rf"(?P<resolved_address>{HEX})\+0x(?P<resolved_size>[0-9a-fA-F]+) "
    r"(?P<resolved_width>\d+)x(?P<resolved_height>\d+) fmt (?P<resolved_format>\d+)"
    r" layer (?P<layer>\d+)$"
)


def fields(match, line):
    """Keep addresses as strings so JSON readers do not lose integer precision."""
    result = {"line": line}
    strings = {"stage", "access", "flags", "kind", "state"}
    for name, value in match.groupdict().items():
        if value is None:
            continue
        if name.endswith("address"):
            result[name] = f"0x{int(value, 16):012x}"
        elif name in strings:
            result[name] = value.strip()
        else:
            result["size_bytes" if name == "size" else name] = int(
                value, 16 if name.endswith("size") or name == "mapped" else 10
            )
    return result


def operation(number, line):
    return {
        "op": number, "line": line, "kind": "unknown", "header": None,
        "shaders": [], "texture_bindings": [], "render_targets": [],
        "depth_attachments": [], "buffer_bindings": [], "constant_references": [],
        "user_data": [], "raster_evidence": [], "skip_reasons": [], "memory_writes": [],
    }


def parse_operation(op, text, line):
    if text.startswith(("draw ", "dispatch ", "dispatch indirect ", "present source ")):
        op["kind"] = text.split()[0]
        op["header"] = text
        groups = re.match(r"dispatch groups=(\d+)x(\d+)x(\d+)\b", text)
        if groups and any(int(group) == 0 for group in groups.groups()):
            op["skip_reasons"].append({"line": line, "reason": "zero dispatch group count"})
    # These BeginOp records are emitted by graphicsRun and bufferCache, between
    # normal draws/dispatches. Memory copies must invalidate image provenance too.
    for prefix, kind in (("wait ", "wait"), ("write data ", "write_data"),
                         ("end of pipe ", "end_of_pipe"), ("dma ", "dma")):
        if text.startswith(prefix):
            op["kind"], op["header"] = kind, text
            break
    memory = re.fullmatch(rf"write data ({HEX}) <- {HEX}(?: {HEX})? \((\d+) dwords\) queue -?\d+", text)
    if memory:
        op["memory_writes"].append({"address": f"0x{int(memory[1], 16):012x}",
                                    "size_bytes": int(memory[2]) * 4, "line": line})
    memory = re.fullmatch(rf"end of pipe ({HEX}) <- {HEX} \((\d+) bytes, .*\) queue -?\d+", text)
    if memory:
        op["memory_writes"].append({"address": f"0x{int(memory[1], 16):012x}",
                                    "size_bytes": int(memory[2]), "line": line})
    memory = re.fullmatch(rf"dma ({HEX})( \(gds\))? <- {HEX}(?: \(gds\))?, 0x([0-9a-fA-F]+) bytes, .*", text)
    if memory and not memory[2]:
        op["memory_writes"].append({"address": f"0x{int(memory[1], 16):012x}",
                                    "size_bytes": int(memory[3], 16), "line": line})
    if re.match(r"(?:not run:|not drawn:|skipped:|consumed as\b)", text):
        op["skip_reasons"].append({"line": line, "reason": text})
        return
    match = re.fullmatch(rf"({STAGE}) ([0-9a-fA-F]{{16}}): .*", text)
    if match:
        op["shaders"].append({"stage": match[1], "hash": match[2].lower(), "line": line})
    match = re.fullmatch(r"shaders: vertex(.*?) ps ([0-9a-fA-F]{16}) topology (\d+)", text)
    if match:
        for shader in re.findall(r"\b[0-9a-fA-F]{16}\b", match[1]):
            op["shaders"].append({"stage": "vertex", "hash": shader.lower(), "line": line})
        if int(match[2], 16):
            op["shaders"].append({"stage": "ps", "hash": match[2].lower(), "line": line})
    for pattern, destination in ((TEXTURE, "texture_bindings"), (RT, "render_targets"),
                                 (DEPTH, "depth_attachments"), (BUFFER, "buffer_bindings"),
                                 (POINTER, "constant_references")):
        match = pattern.fullmatch(text)
        if match:
            op[destination].append(fields(match, line))
            return
    match = FOUND_IMAGE.fullmatch(text)
    if match:
        found = fields(match, line)
        for binding in reversed(op["texture_bindings"]):
            if (binding["stage"], binding["slot"]) == (found["stage"], found["slot"]):
                binding["found_image"] = found
                break
        return
    match = re.fullmatch(r"rt slot (\d+) blend: .*write mask 0x([0-9a-fA-F]+),.*", text)
    if match:
        for target in op["render_targets"]:
            if target["slot"] == int(match[1]):
                target["write_mask"] = int(match[2], 16)
                target["blend_evidence"] = {"line": line, "text": text}
        return
    match = PRESENT.fullmatch(text)
    if match:
        data = fields(match, line)
        source = {key: data[key] for key in ("address", "size_bytes", "width", "height", "format")}
        resolved = {key: data["resolved_" + key] for key in ("address", "width", "height", "format")}
        resolved["size_bytes"] = data["resolved_size"]
        resolved.update(line=line, layer=data["layer"], mip=0, mip_count=1, layer_count=1, depth=1)
        source["line"] = line
        op["present"] = {"source": source, "resolved": resolved,
                         "pre_hud_color_verified": False}
        return
    if re.match(rf"{STAGE} user s\d+:", text):
        op["user_data"].append({"line": line, "text": text})
    elif text.startswith(("raster:", "depth clear value", "depth attachment:")):
        op["raster_evidence"].append({"line": line, "text": text})


def image_key(resource):
    # Unknown/multiple subresources or an alternate backing cannot safely be aliased.
    if (resource.get("found_image") or resource.get("mip_count", 1) != 1
            or resource.get("layer_count", 1) != 1 or resource.get("samples", 1) != 1):
        return None
    if resource.get("mip") is None or resource.get("layer") is None:
        return None
    return tuple(resource.get(key, 1 if key == "depth" else None) for key in
                 ("address", "size_bytes", "width", "height", "depth", "format", "mip", "layer"))


def overlaps(first, second):
    a, b = int(first["address"], 16), int(second["address"], 16)
    return a < b + second["size_bytes"] and b < a + first["size_bytes"]


def resource_ref(resource, role):
    return dict(resource, role=role)


def finish_capture(capture):
    ops = list(capture.pop("_operations").values())
    issues = capture["issues"]
    if not capture["frame_end_seen"]:
        issues.append("missing_end_of_frame")
    if not capture["end_seen"]:
        issues.append("missing_end_capture")
    expected = capture.get("reported_operations")
    numbers = [op["op"] for op in ops]
    if expected is not None and (len(numbers) != expected
                                or any(number != index for index, number in enumerate(numbers, 1))):
        issues.append("operation_sequence_incomplete_or_out_of_order")
    if any(op["header"] is None for op in ops):
        issues.append("unrecognized_or_missing_operation_header")
    capture["complete"] = not issues and not capture["truncated"]
    capture["dlss_ready"] = False
    capture["missing_requirements"] = list(MISSING)
    capture["operations"] = ops
    links, unresolved, candidates, graph_gaps = [], [], [], []
    active_writers = {}
    # The following state exists only in this capture; no historical-frame identity is assumed.
    for op in ops:
        reads = [resource_ref(r, "texture") for r in op["texture_bindings"] if r["access"] == "read"]
        writes = [resource_ref(r, "texture") for r in op["texture_bindings"] if r["access"] == "write"]
        writes.extend(resource_ref(r, "render_target") for r in op["render_targets"] if r.get("write_mask") != 0)
        for depth in op["depth_attachments"]:
            # This old log format does not report the depth attachment's mip/layer.
            if depth["test"] or depth["bounds"] or depth["stencil"]:
                reads.append(resource_ref(depth, "depth_attachment"))
            if depth["write"] or depth["clear"]:
                writes.append(resource_ref(depth, "depth_attachment"))
        if "present" in op:
            reads.append(resource_ref(op["present"]["resolved"], "present_resolved"))
        op["reads"], op["writes"] = reads, writes
        op["eligible_for_graph"] = not op["skip_reasons"] and op["header"] is not None
        op["execution_verified"] = False  # A binding log is not a completion/fence observation.
        if not op["eligible_for_graph"]:
            if (op["header"] is None
                    or any(reason["reason"].startswith("consumed as") for reason in op["skip_reasons"])):
                # A consumed draw/dispatch may become an unlogged clear/fill/copy. Its
                # destination is not reliably described by the normal shader bindings.
                active_writers.clear()
                graph_gaps.append({"op": op["op"], "line": op["line"],
                                   "reason": "unknown_or_consumed_operation_may_change_resource_contents"})
            continue
        for memory in op["memory_writes"]:
            invalidated = [key for key, (_, old) in active_writers.items() if overlaps(memory, old)]
            for key in invalidated:
                del active_writers[key]
            if invalidated:
                graph_gaps.append({"op": op["op"], "line": memory["line"],
                                   "reason": "memory_write_overlaps_previously_written_image",
                                   "range": memory})
        for read in reads:
            key = image_key(read)
            writer = active_writers.get(key) if key is not None else None
            if writer:
                links.append({"producer_op": writer[0], "consumer_op": op["op"],
                              "producer_line": writer[1]["line"], "consumer_line": read["line"],
                              "resource": read, "match": "exact_logged_view",
                              "gpu_dependency_verified": False})
            else:
                unresolved.append({"consumer_op": op["op"], "resource": read,
                                   "reason": "ambiguous_view_or_backing" if key is None else "no_matching_prior_writer_in_capture",
                                   "history_input_verified": False})
        for write in writes:
            # Invalidate older provenance even when this write uses a different format/view.
            for key, (_, old) in list(active_writers.items()):
                if overlaps(write, old):
                    del active_writers[key]
            key = image_key(write)
            if key is not None and write["size_bytes"] and int(write["address"], 16):
                active_writers[key] = (op["op"], write)
        texture_reads = [r for r in reads if r["role"] == "texture"]
        unique_reads = {(r["address"], r["size_bytes"]) for r in texture_reads}
        fullscreen = op["kind"] == "draw" and re.search(r"\bcount=[1-6]\b", op["header"])
        if (op["kind"] == "dispatch" or fullscreen) and len(unique_reads) >= 2 and writes:
            # These structural hints also occur in spatial filters: do not classify the pass as TAA.
            image_writes = [r for r in writes if r["role"] != "depth_attachment" and min(r["width"], r["height"]) > 1]
            if image_writes:
                candidates.append({"op": op["op"], "line": op["line"],
                                   "classification": "multi_input_image_pass_candidate",
                                   "temporal_behavior_verified": False, "motion_vectors_verified": False,
                                   "shaders": op["shaders"],
                                   "evidence": [{"line": r["line"], "role": r["role"],
                                                 "address": r["address"], "width": r["width"], "height": r["height"]}
                                                for r in texture_reads + image_writes],
                                   "constant_reference_lines": [r["line"] for r in op["constant_references"]],
                                   "capture_complete": capture["complete"],
                                   "warning": "Spatial filters and composites can satisfy this heuristic; inspect shaders and numeric captures."})
    ancestors = {}
    for link in links:
        ancestors.setdefault(link["consumer_op"], set()).add(link["producer_op"])
    present_ops = [op["op"] for op in ops if "present" in op and op["eligible_for_graph"]]
    reaches_present = set()
    pending = list(present_ops)
    while pending:
        number = pending.pop()
        if number in reaches_present:
            continue
        reaches_present.add(number)
        pending.extend(ancestors.get(number, ()))
    for candidate in candidates:
        candidate["reaches_logged_present"] = candidate["op"] in reaches_present if present_ops else None
    capture.update(texture_links=links, unresolved_reads=unresolved, temporal_candidates=candidates,
                   present_ops=present_ops, graph_gaps=graph_gaps)
    return capture


def iter_captures(lines):
    """Yield one analysed capture at a time; diagnostics outside capture boundaries are ignored."""
    current = None
    sequence = 0
    for line_number, raw in enumerate(lines, 1):
        # LOGF emits FG at the start of its own line. Do not match quoted or incidental 'FG'.
        match = FG.fullmatch(ANSI.sub("", raw.rstrip("\r\n")).lstrip("\ufeff"))
        if not match:
            continue
        number, text = int(match[1]), match[2].strip()
        begin = BEGIN.fullmatch(text) if number == 0 else None
        if begin:
            if current is not None:
                current["issues"].append("next_capture_before_end")
                yield finish_capture(current)
            sequence += 1
            current = {"sequence": sequence, "capture": int(begin[1]), "after_frame_end": int(begin[2]),
                       "begin_line": line_number, "end_seen": False, "frame_end_seen": False,
                       "truncated": False, "issues": [], "_operations": {}, "deferred_diagnostics": []}
            continue
        if current is None:
            continue
        if DEFERRED_CLEAR.fullmatch(text):
            current["deferred_diagnostics"].append({"reported_op": number, "line": line_number,
                                                     "text": text, "capture_attribution_verified": False})
            continue
        frame_end = FRAME_END.fullmatch(text) if number == 0 else None
        if frame_end:
            if int(frame_end[1]) != current["capture"]:
                current["issues"].append("end_of_frame_capture_mismatch")
            else:
                current["frame_end_seen"] = True
                current["reported_operations"] = int(frame_end[2])
                current["frame_end_line"] = line_number
            current["truncated"] |= "truncated" in frame_end[3]
            continue
        end = END.fullmatch(text) if number == 0 else None
        if end:
            if int(end[1]) != current["capture"]:
                current["issues"].append("end_capture_mismatch")
            else:
                current["end_seen"] = True
            current["end_line"] = line_number
            yield finish_capture(current)
            current = None
            continue
        if "the capture stops logging here:" in text:
            current["truncated"] = True
            continue
        if number == 0:
            continue
        # Samples-passed diagnostics may follow end-of-frame; they must not create new ops.
        if current["frame_end_seen"]:
            continue
        op = current["_operations"].setdefault(number, operation(number, line_number))
        parse_operation(op, text, line_number)
    if current is not None:
        yield finish_capture(current)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", required=True, type=Path, help="Kyty printf log with FG captures")
    parser.add_argument("--output", required=True, type=Path, help="JSON analysis output")
    args = parser.parse_args(argv)
    if args.log.resolve() == args.output.resolve():
        parser.error("--output must not overwrite --log")
    count = complete = 0
    try:
        # Decode damaged log bytes visibly; a malformed marker must not become valid evidence.
        with args.log.open("r", encoding="utf-8-sig", errors="replace") as log:
            with args.output.open("w", encoding="utf-8", newline="\n") as output:
                header = {"schema_version": SCHEMA_VERSION, "source_log": str(args.log.resolve()),
                          "dlss_ready": False, "missing_requirements": MISSING,
                          "limitations": ["Candidates are not verified temporal passes or motion vectors.",
                                          "Exact logged views only; aliases and cross-capture history are not inferred.",
                                          "Binding records do not prove execution or complete pixel writes.",
                                          "Presented color may already include HUD and temporal processing." ]}
                output.write(json.dumps(header, ensure_ascii=False)[:-1] + ', "captures": [\n')
                for capture in iter_captures(log):
                    if count:
                        output.write(",\n")
                    json.dump(capture, output, ensure_ascii=False, indent=2)
                    count += 1
                    complete += int(capture["complete"])
                output.write('\n], "summary": ')
                json.dump({"captures": count, "complete_captures": complete,
                           "incomplete_captures": count - complete}, output)
                output.write("}\n")
    except OSError as error:
        print(f"Analysis failed: {error}", file=sys.stderr)
        return 1
    print(f"Analysed {count} captures ({complete} complete); DLSS inputs remain unverified: {args.output}")
    return 0 if count else 2


if __name__ == "__main__":
    sys.exit(main())
