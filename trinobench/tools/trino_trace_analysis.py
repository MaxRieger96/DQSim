#!/usr/bin/env python3
"""
Summarize a Trino query-info / trace JSON in a terminal-friendly format.

Usage:
  python3 trino_trace_summary.py query.json
  python3 trino_trace_summary.py query.json --details 20

The first output block is DOT. Paste it into https://dreampuf.github.io/GraphvizOnline/
or WebGraphviz to render the execution plan.
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import math
import re
import sys
from collections import defaultdict
from typing import Any, Iterable


DUR_RE = re.compile(r"^([0-9]+(?:\.[0-9]+)?)\s*(ns|us|µs|ms|s|m|h|d)$")
SIZE_RE = re.compile(r"^([0-9]+(?:\.[0-9]+)?)\s*(B|kB|KB|MB|GB|TB|PB)$")

DUR_MULT = {"ns": 1e-9, "us": 1e-6, "µs": 1e-6, "ms": 1e-3, "s": 1.0, "m": 60.0, "h": 3600.0, "d": 86400.0}
SIZE_MULT = {"B": 1, "kB": 1000, "KB": 1000, "MB": 1000**2, "GB": 1000**3, "TB": 1000**4, "PB": 1000**5}


def parse_time(s: Any) -> dt.datetime | None:
    if not s or not isinstance(s, str):
        return None
    try:
        return dt.datetime.fromisoformat(s.replace("Z", "+00:00"))
    except ValueError:
        # Trino sometimes emits millisecond timestamps without full nanosecond precision issues.
        return None


def seconds_since(t: Any, zero: dt.datetime | None) -> str:
    ts = parse_time(t)
    if not ts or not zero:
        return "-"
    return f"{(ts - zero).total_seconds():.3f}s"


def dur_s(v: Any) -> float:
    if isinstance(v, (int, float)):
        return float(v)
    if isinstance(v, dict):
        # Distribution fields in task pipeline stats are nanoseconds.
        return float(v.get("total", 0.0)) * 1e-9
    if not isinstance(v, str):
        return 0.0
    m = DUR_RE.match(v.strip())
    return float(m.group(1)) * DUR_MULT[m.group(2)] if m else 0.0


def size_b(v: Any) -> float:
    if isinstance(v, (int, float)):
        return float(v)
    if not isinstance(v, str):
        return 0.0
    m = SIZE_RE.match(v.strip())
    return float(m.group(1)) * SIZE_MULT[m.group(2)] if m else 0.0


def fmt_dur(v: Any) -> str:
    s = dur_s(v)
    if s == 0:
        return "0"
    if s < 1e-3:
        return f"{s * 1e6:.1f}us"
    if s < 1:
        return f"{s * 1e3:.1f}ms"
    return f"{s:.2f}s"


def fmt_size(v: Any) -> str:
    b = size_b(v)
    units = ["B", "kB", "MB", "GB", "TB", "PB"]
    for u in units:
        if abs(b) < 1000 or u == units[-1]:
            return f"{b:.0f}{u}" if u == "B" else f"{b:.2f}{u}"
        b /= 1000
    return f"{b:.0f}B"


def fmt_rows(n: Any) -> str:
    try:
        n = float(n)
    except Exception:
        return "-"
    if n >= 1e9:
        return f"{n/1e9:.2f}B"
    if n >= 1e6:
        return f"{n/1e6:.2f}M"
    if n >= 1e3:
        return f"{n/1e3:.2f}k"
    return str(int(n))


def table(headers: list[str], rows: Iterable[Iterable[Any]]) -> str:
    rows = [[str(c) for c in r] for r in rows]
    widths = [len(h) for h in headers]
    for r in rows:
        for i, c in enumerate(r):
            widths[i] = max(widths[i], len(c))
    def line(vals: list[str]) -> str:
        return "  ".join(vals[i].ljust(widths[i]) for i in range(len(vals)))
    return "\n".join([line(headers), line(["-" * w for w in widths]), *[line(r) for r in rows]])


def stages_of(q: dict[str, Any]) -> list[dict[str, Any]]:
    s = q.get("stages", [])
    return s.get("stages", []) if isinstance(s, dict) else s


def stage_num(stage_id: str | int) -> int:
    try:
        return int(str(stage_id).split(".")[-1])
    except Exception:
        return -1


def plan_json(stage: dict[str, Any]) -> dict[str, Any] | None:
    rep = stage.get("plan", {}).get("jsonRepresentation")
    if rep:
        try:
            return json.loads(rep)
        except Exception:
            pass
    return None


def walk_plan(node: dict[str, Any], out: list[dict[str, Any]]) -> None:
    out.append(node)
    for child in node.get("children", []) or []:
        walk_plan(child, out)


def dot_escape(s: Any) -> str:
    return str(s).replace('\\', '\\\\').replace('"', '\\"').replace("\n", "\\n")


def operator_index(stages: list[dict[str, Any]]) -> dict[tuple[int, str], list[dict[str, Any]]]:
    idx: dict[tuple[int, str], list[dict[str, Any]]] = defaultdict(list)
    for st in stages:
        sn = stage_num(st.get("stageId", ""))
        for op in st.get("stageStats", {}).get("operatorSummaries", []):
            idx[(sn, str(op.get("planNodeId")))].append(op)
    return idx


def pipeline_label(ops: list[dict[str, Any]]) -> str:
    if not ops:
        return "p?"
    pids = sorted({str(o.get("pipelineId", "?")) for o in ops})
    names = ",".join(sorted({o.get("operatorType", "?").replace("Operator", "") for o in ops}))
    cpu = sum(dur_s(o.get("getOutputCpu")) + dur_s(o.get("addInputCpu")) + dur_s(o.get("finishCpu")) for o in ops)
    out = sum(size_b(o.get("outputDataSize")) for o in ops)
    return f"p{','.join(pids)}\\n{names}\\ncpu={fmt_dur(cpu)} out={fmt_size(out)}"


def make_dot(stages: list[dict[str, Any]], qid: str) -> str:
    op_idx = operator_index(stages)
    lines = ["digraph trino_query {", "  rankdir=BT;", "  node [shape=box, fontsize=10, fontname=Helvetica];", f'  label="Trino query {dot_escape(qid)}";']
    remote_edges: list[tuple[str, str, str]] = []
    for st in stages:
        sn = stage_num(st.get("stageId", ""))
        root = plan_json(st)
        if not root:
            continue
        nodes: list[dict[str, Any]] = []
        walk_plan(root, nodes)
        lines.append(f'  subgraph cluster_stage_{sn} {{ label="stage {sn}";')
        for n in nodes:
            nid = str(n.get("id"))
            ops = op_idx.get((sn, nid), [])
            label = f"stage {sn} node {nid}\\n{n.get('name', n.get('@type', '?'))}"
            if ops:
                label += "\\n" + pipeline_label(ops)
            lines.append(f'    s{sn}_n{dot_escape(nid)} [label="{dot_escape(label)}"];')
        for n in nodes:
            nid = str(n.get("id"))
            for c in n.get("children", []) or []:
                cid = str(c.get("id"))
                edge_label = ""
                ops = op_idx.get((sn, cid), [])
                if ops:
                    out_b = sum(size_b(o.get("outputDataSize")) for o in ops)
                    out_r = sum(float(o.get("outputPositions", 0) or 0) for o in ops)
                    edge_label = f' [label="{fmt_size(out_b)} / {fmt_rows(out_r)} rows"]'
                lines.append(f'    s{sn}_n{dot_escape(cid)} -> s{sn}_n{dot_escape(nid)}{edge_label};')
                if c.get("name") == "RemoteSource" or c.get("@type") == "remoteSource":
                    for src in c.get("descriptor", {}).get("sourceFragmentIds", "").strip("[]").split(","):
                        src = src.strip()
                        if src.isdigit():
                            remote_edges.append((f"s{src}_stage_out", f"s{sn}_n{cid}", f"remote {src}->{sn}"))
        lines.append("  }")
        # Invisible-ish anchor for inter-stage remote edges.
        lines.append(f'  s{sn}_stage_out [shape=point, label=""];')
    for a, b, lab in remote_edges:
        lines.append(f'  {a} -> {b} [style=dashed, label="{dot_escape(lab)}"];')
    lines.append("}")
    return "\n".join(lines)


def query_summary(q: dict[str, Any]) -> str:
    qs = q.get("queryStats", {})
    fields = [
        ("query id", q.get("queryId", "-")),
        ("state", q.get("state", "-")),
        ("elapsed", qs.get("elapsedTime", "-")),
        ("execution", qs.get("executionTime", "-")),
        ("queued", qs.get("queuedTime", "-")),
        ("planning", qs.get("planningTime", "-")),
        ("cpu", qs.get("totalCpuTime", "-")),
        ("scheduled", qs.get("totalScheduledTime", "-")),
        ("blocked", qs.get("totalBlockedTime", "-")),
        ("physical input", f"{qs.get('physicalInputDataSize','-')} / {fmt_rows(qs.get('physicalInputPositions'))} rows"),
        ("processed input", f"{qs.get('processedInputDataSize','-')} / {fmt_rows(qs.get('processedInputPositions'))} rows"),
        ("network input", f"{qs.get('internalNetworkInputDataSize','-')} / {fmt_rows(qs.get('internalNetworkInputPositions'))} rows"),
        ("output", f"{qs.get('outputDataSize','-')} / {fmt_rows(qs.get('outputPositions'))} rows"),
        ("peak memory", qs.get("peakTotalMemoryReservation", qs.get("peakUserMemoryReservation", "-"))),
        ("spilled", qs.get("spilledDataSize", "-")),
        ("tasks", f"{qs.get('completedTasks','?')}/{qs.get('totalTasks','?')} complete"),
        ("drivers", f"{qs.get('completedDrivers','?')}/{qs.get('totalDrivers','?')} complete"),
    ]
    return table(["metric", "value"], fields)


def stage_rows(stages: list[dict[str, Any]], zero: dt.datetime | None) -> list[list[str]]:
    rows = []
    for st in sorted(stages, key=lambda s: stage_num(s.get("stageId", ""))):
        ss = st.get("stageStats", {})
        starts = [p.get("firstStartTime") for t in st.get("tasks", []) for p in t.get("stats", {}).get("pipelines", [])]
        ends = [p.get("lastEndTime") for t in st.get("tasks", []) for p in t.get("stats", {}).get("pipelines", [])]
        first = min([parse_time(x) for x in starts if parse_time(x)] or [None])
        last = max([parse_time(x) for x in ends if parse_time(x)] or [None])
        rows.append([
            str(stage_num(st.get("stageId", ""))), st.get("state", "-"),
            seconds_since(first.isoformat() if first else None, zero), seconds_since(last.isoformat() if last else None, zero),
            fmt_dur(ss.get("totalCpuTime")), fmt_dur(ss.get("totalScheduledTime")), fmt_dur(ss.get("totalBlockedTime")),
            fmt_size(ss.get("physicalInputDataSize")), fmt_size(ss.get("processedInputDataSize")), fmt_size(ss.get("internalNetworkInputDataSize")), fmt_size(ss.get("outputDataSize")),
            fmt_rows(ss.get("processedInputPositions")), str(ss.get("totalTasks", len(st.get("tasks", []))))
        ])
    return rows


def pipeline_rows(stages: list[dict[str, Any]], zero: dt.datetime | None) -> list[list[str]]:
    agg: dict[tuple[int, int], dict[str, Any]] = defaultdict(lambda: defaultdict(float))
    times: dict[tuple[int, int], dict[str, list[dt.datetime]]] = defaultdict(lambda: {"start": [], "end": []})
    flags: dict[tuple[int, int], set[str]] = defaultdict(set)
    for st in stages:
        sn = stage_num(st.get("stageId", ""))
        for task in st.get("tasks", []):
            for p in task.get("stats", {}).get("pipelines", []):
                key = (sn, int(p.get("pipelineId", -1)))
                a = agg[key]
                for k in ["totalDrivers", "completedDrivers", "physicalInputDataSize", "processedInputDataSize", "internalNetworkInputDataSize", "outputDataSize", "processedInputPositions", "outputPositions"]:
                    a[k] += size_b(p.get(k)) if "DataSize" in k else float(p.get(k, 0) or 0)
                a["cpu"] += dur_s(p.get("totalCpuTime")); a["sched"] += dur_s(p.get("totalScheduledTime")); a["blocked"] += dur_s(p.get("totalBlockedTime"))
                s, e = parse_time(p.get("firstStartTime")), parse_time(p.get("lastEndTime"))
                if s: times[key]["start"].append(s)
                if e: times[key]["end"].append(e)
                if p.get("inputPipeline"): flags[key].add("in")
                if p.get("outputPipeline"): flags[key].add("out")
    rows = []
    for key, a in sorted(agg.items()):
        sn, pid = key
        first = min(times[key]["start"]) if times[key]["start"] else None
        last = max(times[key]["end"]) if times[key]["end"] else None
        rows.append([str(sn), str(pid), ",".join(sorted(flags[key])) or "-", seconds_since(first.isoformat() if first else None, zero), seconds_since(last.isoformat() if last else None, zero), fmt_dur(a["cpu"]), fmt_dur(a["sched"]), fmt_dur(a["blocked"]), fmt_size(a["physicalInputDataSize"]), fmt_size(a["processedInputDataSize"]), fmt_size(a["internalNetworkInputDataSize"]), fmt_size(a["outputDataSize"]), fmt_rows(a["processedInputPositions"]), fmt_rows(a["outputPositions"]), f"{int(a['completedDrivers'])}/{int(a['totalDrivers'])}"])
    return rows


def task_rows(stages: list[dict[str, Any]], zero: dt.datetime | None, limit: int) -> list[list[str]]:
    rows = []
    for st in sorted(stages, key=lambda s: stage_num(s.get("stageId", ""))):
        sn = stage_num(st.get("stageId", ""))
        for task in st.get("tasks", []):
            status = task.get("taskStatus", {})
            stats = task.get("stats", {})
            tid = status.get("taskId", "-").split(".")[-2:] if status.get("taskId") else ["-"]
            tid = ".".join(tid)
            rows.append([str(sn), tid, status.get("nodeId", "-"), seconds_since(stats.get("firstStartTime"), zero), seconds_since(stats.get("endTime"), zero), fmt_dur(stats.get("totalCpuTime")), fmt_dur(stats.get("totalScheduledTime")), fmt_dur(stats.get("totalBlockedTime")), fmt_size(stats.get("physicalInputDataSize")), fmt_size(stats.get("processedInputDataSize")), fmt_size(stats.get("internalNetworkInputDataSize")), fmt_size(stats.get("outputDataSize")), fmt_rows(stats.get("processedInputPositions")), str(stats.get("completedDrivers", "-")), stats.get("peakUserMemoryReservation", status.get("peakMemoryReservation", "-"))])
    rows.sort(key=lambda r: (int(r[0]), r[2], r[1]))
    return rows[:limit] if limit > 0 else rows


def operator_rows(stages: list[dict[str, Any]], limit: int) -> list[list[str]]:
    rows = []
    for st in stages:
        sn = stage_num(st.get("stageId", ""))
        for op in st.get("stageStats", {}).get("operatorSummaries", []):
            cpu = dur_s(op.get("addInputCpu")) + dur_s(op.get("getOutputCpu")) + dur_s(op.get("finishCpu"))
            wall = dur_s(op.get("addInputWall")) + dur_s(op.get("getOutputWall")) + dur_s(op.get("finishWall"))
            rows.append([str(sn), str(op.get("pipelineId")), str(op.get("operatorId")), op.get("operatorType", "-"), op.get("planNodeId", "-"), fmt_dur(cpu), fmt_dur(wall), fmt_size(op.get("inputDataSize")), fmt_rows(op.get("inputPositions")), fmt_size(op.get("outputDataSize")), fmt_rows(op.get("outputPositions")), op.get("peakTotalMemoryReservation", "-")])
    rows.sort(key=lambda r: dur_s(r[5]), reverse=True)
    return rows[:limit] if limit > 0 else rows


def main() -> int:
    ap = argparse.ArgumentParser(description="Print DOT + human stats from a Trino query trace JSON")
    ap.add_argument("json_file")
    ap.add_argument("--details", type=int, default=50, help="max task/operator detail rows; 0 means all")
    args = ap.parse_args()

    with open(args.json_file, "r", encoding="utf-8") as f:
        q = json.load(f)

    stages = stages_of(q)
    qs = q.get("queryStats", {})
    zero = parse_time(qs.get("createTime")) or parse_time(q.get("session", {}).get("start"))

    print("WEBGRAPHVIZ / DOT")
    print(make_dot(stages, q.get("queryId", "-")))
    print("\nQUERY SUMMARY")
    print(query_summary(q))
    print("\nSTAGES, GLOBAL TIME FROM QUERY START")
    print(table(["stage", "state", "start", "end", "cpu", "sched", "blocked", "phys in", "processed", "net in", "out", "rows", "tasks"], stage_rows(stages, zero)))
    print("\nPIPELINES, AGGREGATED ACROSS TASKS")
    print(table(["stage", "pipe", "kind", "start", "end", "cpu", "sched", "blocked", "phys in", "processed", "net in", "out", "in rows", "out rows", "drivers"], pipeline_rows(stages, zero)))
    print("\nTOP OPERATORS BY CPU")
    print(table(["stage", "pipe", "op", "type", "node", "cpu", "wall", "in", "in rows", "out", "out rows", "peak mem"], operator_rows(stages, args.details)))
    print("\nTASK / COMPUTE NODE DETAIL")
    print(table(["stage", "task", "node", "start", "end", "cpu", "sched", "blocked", "phys in", "processed", "net in", "out", "rows", "drivers", "peak mem"], task_rows(stages, zero, args.details)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
