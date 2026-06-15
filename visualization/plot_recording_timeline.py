import json
import math
from collections import defaultdict

import matplotlib as mpl
import matplotlib.pyplot as plt
from matplotlib.legend_handler import HandlerBase
from matplotlib.patches import Patch, Rectangle

from visualization.infra import get_figure_format, get_figure_path

NODE = {0: "N1", 1: "N2", 2: "St."}
PIPE = {0: "P1", 1: "P2"}

HATCH_COLOR = "0.8"
BAR_LINEWIDTH = 3
HATCH = {"lat": "", "task": "", "xfer": "//", "shuffle": "\\\\"}
LINECOLOR = {"lat": "grey", "task": "black", "xfer": "black", "shuffle": "black"}

# Keep the 250us latency boxes visible for these transfers.
VISIBLE_TINY_LATENCY = {"R2.1", "R2.2", "R2.3", "R2.4"}

# Co-locate these displayed transfer names with displayed task names.
# Both relation labels and pipeline labels are 1-based.
COLOCATED_TRANSFERS = {
    "R1.2": ("task", 1, 0, 1),  # P1.2
    "R1.4": ("task", 1, 0, 3),  # P1.4
}

STORAGE_NODE = 2
STORAGE_LATENCY_S = 0.1

# Suppress endpoints only for selected non-latency transfer boxes.
SUPPRESS_TRANSFER_LOCATION = {
    ("R2.2", 2, 1, "run"),  # R2.2 St.→N2
    ("R2.4", 2, 1, "run"),  # R2.4 St.→N2
}


def close(a, b):
    return math.isclose(a, b, abs_tol=1e-9)


def label_pipeline(pid, part):
    return f"{PIPE[pid]}.{part + 1}"


def label_node(n):
    return NODE[n]


def relation_name(rel, partition):
    return f"{rel[partition['dataUnitId']]}.{partition['partitionIndex'] + 1}"


def lane_key(lane):
    kind, node = lane[:2]
    if kind == "task":
        _, node, pid, part = lane
        return (node, pid, part)
    if kind == "shuffle":
        return (node, 2, 0)
    return (node, 3, lane)


def add_event(events, kind, name, lane, item, t0, t1, phase, show_tiny=False):
    events.append(
        dict(
            kind=kind,
            name=name,
            lane=lane,
            item=item,
            start=1000 * t0,
            end=1000 * t1,
            phase=phase,
            hidden=close(t1 - t0, 0.00025) and not show_tiny,
        )
    )


def transfer_lane(name, src, dst, part):
    if name in COLOCATED_TRANSFERS:
        return COLOCATED_TRANSFERS[name]
    if name.startswith("R2."):
        return ("task", dst, 1, part)
    if name == "I1.0":
        return ("shuffle", 0)
    return ("xfer", dst, name, src, dst)


def transfer_label(name, src, dst, phase=None):
    if (name, src, dst, phase) in SUPPRESS_TRANSFER_LOCATION:
        return name
    return f"{name} {label_node(src)}→{label_node(dst)}"


def transfer_phase(src, t0, t1, default_phase):
    if src == STORAGE_NODE and t1 <= STORAGE_LATENCY_S:
        return "lat"
    return default_phase


def add_transfer_event(events, name, lane, item, src, dst, t0, t1, default_phase):
    if src == STORAGE_NODE and t0 < STORAGE_LATENCY_S < t1:
        add_event(
            events,
            "xfer",
            transfer_label(name, src, dst, "lat"),
            lane,
            item,
            t0,
            STORAGE_LATENCY_S,
            "lat",
            show_tiny=True,
        )
        add_event(
            events,
            "xfer",
            transfer_label(name, src, dst, "run"),
            lane,
            item,
            STORAGE_LATENCY_S,
            t1,
            "run",
        )
        return

    phase = transfer_phase(src, t0, t1, default_phase)
    add_event(
        events,
        "xfer",
        transfer_label(name, src, dst, phase),
        lane,
        item,
        t0,
        t1,
        phase,
        show_tiny=src == STORAGE_NODE and phase == "lat",
    )


def extract_events(recording):
    rel = {du["id"]: du["name"] for du in recording["problem"]["plan"]["dataUnits"]}
    events = []

    for s0, s1 in zip(recording["states"], recording["states"][1:]):
        t0, t1 = float(s0["runtime"]), float(s1["runtime"])
        dt = t1 - t0
        if dt <= 0:
            continue

        phase = "lat" if close(dt, 0.1) or close(dt, 0.00025) else "run"

        for r in s0.get("runningTasks", []):
            task = r["task"]["task"]
            node = r["task"]["node"]
            pid = task["pipelineId"]
            part = task["outputPartition"]["partitionIndex"]
            add_event(
                events,
                "task",
                label_pipeline(pid, part),
                ("task", node, pid, part),
                ("task", node, pid, part),
                t0,
                t1,
                "run",
            )

        for r in s0.get("runningTransfers", []):
            x = r["transfer"]
            src, dst = x["from"], x["to"]
            p = x["partition"]
            part = p["partitionIndex"]
            name = relation_name(rel, p)
            add_transfer_event(
                events,
                name,
                transfer_lane(name, src, dst, part),
                ("xfer", src, dst, p["dataUnitId"], part),
                src,
                dst,
                t0,
                t1,
                phase,
            )

        for r in s0.get("runningShuffles", []):
            src, dst = r["from"], r["to"]
            add_event(events, "shuffle", "Sh", ("task", 1, 0, 1), ("shuffle", src, dst), t0, t1, phase)

    return events


def merge_events(events):
    by_item = defaultdict(list)
    for e in events:
        by_item[e["item"]].append(e)

    merged = []
    for parts in by_item.values():
        parts.sort(key=lambda e: e["start"])
        cur = None
        bridge_end = None

        for e in parts:
            if e["hidden"]:
                if cur and close(cur["end"], e["start"]):
                    bridge_end = e["end"]
                continue

            if cur is None:
                cur = e.copy()
                continue

            adjacent = close(cur["end"], e["start"])
            bridged = bridge_end is not None and close(bridge_end, e["start"])
            if cur["phase"] == e["phase"] and (adjacent or bridged):
                cur["end"] = e["end"]
                bridge_end = None
            else:
                merged.append(cur)
                cur = e.copy()
                bridge_end = None

        if cur:
            merged.append(cur)

    return merged


def lane_positions(events):
    lanes = sorted({e["lane"] for e in events}, key=lane_key)
    y, prev_node, pos = 0.0, None, {}

    for lane in lanes:
        node = lane[1]
        if prev_node is not None and node != prev_node:
            y += 0.0
        pos[lane] = -y
        prev_node = node
        y += 1.0

    return pos


def draw_bar(ax, x, y, width, kind, phase):
    hatch = HATCH["lat" if phase == "lat" else kind]
    linecolor = LINECOLOR["lat" if phase == "lat" else kind]

    if hatch:
        with mpl.rc_context({"hatch.color": HATCH_COLOR, "hatch.linewidth": 1}):
            ax.add_patch(
                Rectangle(
                    (x, y - 0.47),
                    width,
                    0.94,
                    facecolor="white",
                    edgecolor=HATCH_COLOR,
                    linewidth=0,
                    hatch=hatch,
                    zorder=1,
                )
            )

    ax.add_patch(
        Rectangle(
            (x, y - 0.47),
            width,
            0.94,
            facecolor="none",
            edgecolor=linecolor,
            linewidth=BAR_LINEWIDTH,
            zorder=3,
        )
    )


class HatchedLegendHandler(HandlerBase):
    def create_artists(self, legend, handle, xdescent, ydescent, width, height, fontsize, trans):
        hatch = handle.get_hatch()
        edgecolor = handle.get_edgecolor()
        linewidth = handle.get_linewidth()
        yoff = -0.18 * height

        hatch_rect = Rectangle(
            (xdescent, ydescent + yoff),
            width,
            height,
            facecolor="white",
            edgecolor=HATCH_COLOR,
            linewidth=0,
            hatch=hatch,
            transform=trans,
        )

        outline_rect = Rectangle(
            (xdescent, ydescent + yoff),
            width,
            height,
            facecolor="none",
            edgecolor=edgecolor,
            linewidth=linewidth,
            transform=trans,
        )

        return [hatch_rect, outline_rect]


def add_legend(ax):
    handles = [
        Patch(facecolor="white", edgecolor=LINECOLOR["lat"], linewidth=1, hatch=HATCH["lat"], label="Latency"),
        Patch(facecolor="white", edgecolor=LINECOLOR["task"], linewidth=1, hatch=HATCH["task"], label="Task"),
        Patch(facecolor="white", edgecolor=LINECOLOR["xfer"], linewidth=1, hatch="///", label="Transfer"),
        Patch(facecolor="white", edgecolor=LINECOLOR["shuffle"], linewidth=1, hatch="\\\\\\", label="Shuffle"),
    ]

    ax.legend(
        handles=handles,
        handler_map={Patch: HatchedLegendHandler()},
        loc="lower right",
        fontsize=10,
        frameon=True,
        handlelength=2.5,
        handleheight=0.9,
        borderpad=0.2,
        labelspacing=0.25,
    )


def plot(events, name: str):
    y = lane_positions(events)
    fig, ax = plt.subplots(figsize=(6.0, max(1.8, 0.28 * len(y) + 0.75)))

    for e in sorted(events, key=lambda e: (lane_key(e["lane"]), e["start"])):
        width = e["end"] - e["start"]
        y0 = y[e["lane"]]
        draw_bar(ax, e["start"], y0, width, e["kind"], e["phase"])

        if width >= 15:
            ax.text(
                e["start"] + width / 2,
                y0,
                e["name"],
                ha="center",
                va="center",
                fontsize=10,
                clip_on=True,
                zorder=4,
            )
        elif width >= 4:
            ax.text(
                e["start"] + width / 2,
                y0,
                e["name"],
                ha="center",
                va="center",
                fontsize=4.8,
                rotation=90,
                clip_on=True,
                zorder=4,
            )

    node_rows = defaultdict(list)
    for lane, y0 in y.items():
        node_rows[lane[1]].append(y0)

    for node, rows in sorted(node_rows.items()):
        y_pos = sum(rows) / len(rows)
        ax.text(
            -0.012,
            y_pos,
            label_node(node),
            transform=ax.get_yaxis_transform(),
            ha="right",
            va="center",
            fontsize=10,
            fontweight="bold",
        )

    xs = sorted({e["start"] for e in events} | {e["end"] for e in events})
    xticks = []
    for x in xs:
        if not xticks or x - xticks[-1] > 0.25 + 1e-9:
            xticks.append(x)
    ax.set_xticks(xticks)
    ax.set_xticklabels([f"{x:.0f}" for x in xticks])
    ax.set_xlim(min(xs) - 5, max(xs) + 5)
    ax.set_ylim(min(y.values()) - 0.52, max(y.values()) + 0.52)
    ax.set_yticks([])
    ax.set_xlabel("ms", fontsize=7, labelpad=1)
    ax.tick_params(axis="x", labelsize=6, length=2, pad=1)
    ax.grid(axis="x", linewidth=0.25, alpha=0.25)

    for spine in ("top", "right", "left"):
        ax.spines[spine].set_visible(False)
    ax.spines["bottom"].set_linewidth(0.5)

    add_legend(ax)

    fig.tight_layout(pad=0.15)
    plt.savefig(f"{get_figure_path()}/{name}.pdf", bbox_inches="tight")


def plot_timeline(name: str):
    with open("visualization/example_recording.json") as f:
        recording = json.load(f)
    plot(merge_events(extract_events(recording)), name)


def main():
    plot_timeline("timeline")


if __name__ == "__main__":
    main()
