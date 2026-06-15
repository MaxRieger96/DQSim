import json
import os
import random
import shutil
import subprocess
import sys
import tempfile
import zipfile
from collections import defaultdict
from pathlib import Path
from typing import Optional

import matplotlib.ticker as mticker
import numpy as np
from matplotlib import pyplot as plt

from visualization.benchmark_loading import get_benchmarks, merge_data
from visualization.eval_benchmark import SimulationBench, geo_mean, get_data, parse_benchmarks
from visualization.infra import (
    compile_all,
    compile_all_debug,
    configure_cmake,
    download_artifact,
    extract_zip,
    get_figure_format,
    get_figure_path,
    set_figure_format,
    set_figure_path,
    set_use_latex,
    setup_matplotlib_latex_font,
)
from visualization.plot_recording_timeline import plot_timeline


def load_all_problem_files():
    # loads the problems from the provided zip instead of creating them anew
    download_artifact("problems.zip")
    extract_zip("downloaded_data/problems.zip", "problems", "problems")


def run_benchmark(definition_file: Path):
    # compile runner
    runner_executable = Path("build/ds_bench_s")
    assert runner_executable.exists(), f"Executable {runner_executable} does not exist"

    # Open file as json and check whether name in json is identical to file name
    with open(definition_file, "r") as f:
        data = json.load(f)
        assert data["name"] == definition_file.stem, (
            f"Name in json ({data['name']}) does not match file name ({definition_file.stem})"
        )
    # Call executable
    print(f"{runner_executable} {definition_file}")
    subprocess.run(
        [runner_executable, definition_file], cwd=os.getcwd(), stdout=sys.stdout, stderr=sys.stderr, check=True
    )


def get_benchmark_definition_path() -> Path:
    return Path("visualization/benchmarks/")


def run_all_benchmarks():
    for bench_name in get_benchmark_definition_path().iterdir():
        print(bench_name)
        run_benchmark(bench_name)


def generic_bar_chart(name: str, x_tick_font_size: Optional[int] = None):
    names, _, data = get_benchmarks(name)

    fig, ax = plt.subplots(figsize=(6, 2.1))

    y = []
    for d in data:
        y.append(1 / geo_mean([b.execution_time for b in d]))
    ax.bar(names, y, edgecolor="black")
    ax.set_ylabel("Throughput\nin Queries/s\n(Geometric Mean)")
    if x_tick_font_size is not None:
        ax.tick_params(axis="x", labelsize=x_tick_font_size)
    Path(get_figure_path()).mkdir(parents=True, exist_ok=True)
    plt.savefig(f"{get_figure_path()}/{name}.{get_figure_format()}", bbox_inches="tight")


def state_sep_chart(name: str, x_tick_font_size: Optional[int] = None):
    names, _, data = get_benchmarks(name)

    grouped = defaultdict(dict)
    for n, d in zip(names, data):
        base, broadcast = (n[:-2], True) if n.endswith("+B") else (n, False)
        grouped[base][broadcast] = 1 / geo_mean([b.execution_time for b in d])

    labels = list(grouped)
    x, width = np.arange(len(labels)), 0.35
    fig, ax = plt.subplots(figsize=(6, 2.1))

    for i, (broadcast, label, hatch) in enumerate(
        [
            (False, "No Broadcast", ""),
            (True, "Broadcast", "//"),
        ]
    ):
        ax.bar(
            x + (i - 0.5) * width,
            [grouped[l].get(broadcast, np.nan) for l in labels],
            width,
            label=label,
            hatch=hatch,
            edgecolor="black",
        )

    ax.set_ylabel("Throughput\nin Queries/s\n(Geometric Mean)")
    ax.set_xticks(x, labels)
    if x_tick_font_size is not None:
        ax.tick_params(axis="x", labelsize=x_tick_font_size)
    ax.legend(borderaxespad=0.2, borderpad=0.15, labelspacing=0.2, handletextpad=0.1, handlelength=1.0)

    Path(get_figure_path()).mkdir(parents=True, exist_ok=True)
    plt.savefig(f"{get_figure_path()}/{name}.{get_figure_format()}", bbox_inches="tight")


def by_workload(benches: list[SimulationBench]):
    grouped = defaultdict(list)
    for b in benches:
        grouped[b.workload].append(b)
    return grouped


def generic_bar_chart_by_workload(name: str):
    names, _, data = get_benchmarks(name)

    workloads = ["tpch", "tpcds", "job"]
    labels = ["TPC-H", "TPC-DS", "JOB"]
    hatches = ["", "//", "\\\\"]

    grouped = [by_workload(d) for d in data]

    fig, ax = plt.subplots(figsize=(6, 2.1))
    x, width = np.arange(len(names)), 0.25

    for i, (workload, label, hatch) in enumerate(zip(workloads, labels, hatches)):
        ax.bar(
            x + (i - 1) * width,
            [1 / geo_mean([b.execution_time for b in g[workload]]) for g in grouped],
            width,
            label=label,
            hatch=hatch,
            edgecolor="black",
        )

    ax.set_ylabel("Throughput\nin Queries/s\n(Geometric Mean)")
    ax.set_xticks(x, names)
    ax.legend(
        borderaxespad=0.2,
        borderpad=0.15,
        labelspacing=0.2,
        handletextpad=0.1,
        handlelength=1.0,
        # loc="lower right",
        # Anchor the legend just outside the right edge (x=1.02) and vertically centered (y=0.5).
        # loc="center left",
        # bbox_to_anchor=(1.02, 0.5),
    )

    Path(get_figure_path()).mkdir(parents=True, exist_ok=True)
    plt.savefig(f"{get_figure_path()}/{name}_workload.{get_figure_format()}", bbox_inches="tight")


def log_bar_chart(name: str):
    names, _, data = get_benchmarks(name)

    fig, ax = plt.subplots(figsize=(6, 2.1))

    y = []
    for i, d in enumerate(data):
        print(f"Sum of {names[i]}: {sum([b.execution_time for b in d])}")
        print(f"  Geometric Mean: {geo_mean([b.execution_time for b in d])}")
        print(f"  Throughput: {1 / np.average([b.execution_time for b in d])}")
        print(f"  Geometric Mean Throughput: {1 / geo_mean([b.execution_time for b in d])}")
        y.append(1 / geo_mean([b.execution_time for b in d]))
    ax.bar(names, y, edgecolor="black")

    # write little numbers on top of bars
    for i, val in enumerate(y):
        ax.annotate(
            f"{val:.1f}",
            xy=(i, val),
            xycoords="data",
            xytext=(0, 2),
            textcoords="offset points",
            ha="center",
            va="bottom",
            fontsize=8,
        )
    ax.set_ylabel("Throughput\nin Queries/s\n(Geometric Mean)")
    ax.set_yscale("log")
    Path(get_figure_path()).mkdir(parents=True, exist_ok=True)
    plt.savefig(f"{get_figure_path()}/{name}.{get_figure_format()}", bbox_inches="tight")


def partition_scaling_chart():
    name = "scaling_partitions"
    n1, _, d1 = get_benchmarks(name)
    n2, _, d2 = get_benchmarks("scaling_partitions_heft")
    n3, _, d3 = get_benchmarks("scaling_partitions_random")
    names, data = merge_data([n1, n2, n3], [d1, d2, d3])

    fig, ax = plt.subplots(figsize=(6, 2.5))
    markers = ["o", "s", "^", "D", "P", "X"]

    for n, d, m in zip(names, data, markers):
        partition_count_map = {}
        for b in d:
            if b.n_partitions not in partition_count_map:
                partition_count_map[b.n_partitions] = []
            partition_count_map[b.n_partitions].append(b.scheduling_time)
        x = sorted(list(partition_count_map.keys()))
        y = [geo_mean(partition_count_map[i]) * 1000 for i in x]
        ax.plot(x, y, label=f"{n}", marker=m)

    ax.set_xscale("log", base=2)
    ax.set_yscale("log")

    x_ticks = [2**i for i in range(1, 13)]
    x_tick_names = [str(x) for x in x_ticks]  # + ["1k", "2k", "4k"]
    # x_tick_names[6] = ""
    # x_tick_names[8] = ""
    x_tick_names = np.array(x_tick_names)
    x_tick_names[1::2] = ""
    assert len(x_ticks) == len(x_tick_names)
    ax.set_xticks(x_ticks, x_tick_names)
    y_ticks = [-1, 0, 1, 2]
    y_ticks = [10**i for i in y_ticks]
    ax.set_yticks(y_ticks, [f"${t}$ ms" for t in y_ticks])

    ax.set_ylim(None, 100)
    ax.legend(
        borderaxespad=0.2,
        borderpad=0.15,
        labelspacing=0.2,
        handletextpad=0.1,
        handlelength=1.0,
        # loc="lower right",
        # Anchor the legend just outside the right edge (x=1.02) and vertically centered (y=0.5).
        loc="center left",
        bbox_to_anchor=(1.02, 0.5),
    )
    ax.set_ylabel("Scheduling Time\n(Geometric Mean)")
    ax.set_xlabel("Partition Count")

    Path(get_figure_path()).mkdir(parents=True, exist_ok=True)
    plt.savefig(f"{get_figure_path()}/{name}.{get_figure_format()}", bbox_inches="tight")


def node_scaling_chart():
    name = "scaling_nodes"
    n1, _, d1 = get_benchmarks(name)
    n2, _, d2 = get_benchmarks("scaling_nodes_random")
    names, data = merge_data([n1, n2], [d1, d2])
    fig, ax = plt.subplots(figsize=(6, 2.5))
    markers = ["o", "s", "^", "D", "P", "X"]

    for n, d, m in zip(names, data, markers):
        node_count_map = {}
        for b in d:
            if b.n_nodes not in node_count_map:
                node_count_map[b.n_nodes] = []
            node_count_map[b.n_nodes].append(b.scheduling_time)
        x = sorted(list(node_count_map.keys()))
        y = [geo_mean(node_count_map[i]) * 1000 for i in x]
        ax.plot(x, y, label=f"{n}", marker=m)

    ax.set_xscale("log", base=2)
    ax.set_yscale("log")

    n_x_ticks = 11
    x_ticks = list(2**i for i in range(1, n_x_ticks))
    x_tick_names = [f"${t}$" if i % 2 == 0 else "" for i, t in enumerate(x_ticks)]
    ax.set_xticks(x_ticks, x_tick_names)
    y_ticks = [0, 1, 2, 3]
    y_ticks = [10**i for i in y_ticks]

    def format_ms(t):
        if t < 1000:
            return f"${t}$ ms"
        return f"${t // 1000}$ s"

    ax.set_yticks(y_ticks, [format_ms(t) for t in y_ticks])

    ax.set_ylim(None, 1000)

    # ax.legend(loc="lower right")
    ax.legend(
        borderaxespad=0.2,
        borderpad=0.15,
        labelspacing=0.2,
        handletextpad=0.1,
        handlelength=1.0,
        # Anchor the legend just outside the right edge (x=1.02) and vertically centered (y=0.5).
        loc="center left",
        bbox_to_anchor=(1.02, 0.5),
    )
    ax.set_ylabel("Scheduling Time\n(Geometric Mean)")
    ax.set_xlabel("Node Count")

    Path(get_figure_path()).mkdir(parents=True, exist_ok=True)
    plt.savefig(f"{get_figure_path()}/{name}.{get_figure_format()}", bbox_inches="tight")


def full_enumeration_chart():
    name = "full_enumeration"
    names, _, data = get_benchmarks(name)
    fig, ax = plt.subplots(figsize=(6, 2))

    # Group by algorithm and query
    best_query_time = {}
    worst_query_time = {}
    algorithm_query_time = {n: {} for n in names}

    for n, d in zip(names, data):
        for b in d:
            if b.query_name not in best_query_time or best_query_time[b.query_name] > b.execution_time:
                best_query_time[b.query_name] = b.execution_time
            if b.query_name not in worst_query_time or worst_query_time[b.query_name] < b.execution_time:
                worst_query_time[b.query_name] = b.execution_time
            algorithm_query_time[n][b.query_name] = b.execution_time

    query_names = [n for n in best_query_time]

    noise = [(random.random() - 0.5) * 0.5 for _ in query_names]
    for i_alg, alg in enumerate(names):
        y = [algorithm_query_time[alg][n] / best_query_time[n] for n in query_names]
        x = [i_alg + 1 + noise[i] for i in range(len(y))]
        ax.scatter(x, y, label=alg, alpha=0.5)

    y = [worst_query_time[n] / best_query_time[n] for n in query_names]
    x = [len(names) + 1 + noise[i] for i in range(len(y))]
    ax.scatter(x, y, label="Worst", alpha=0.5)
    names.append("Worst")

    ax.set_yscale("log", base=10)
    ax.yaxis.set_major_formatter(mticker.FuncFormatter(lambda y, _: f"{y:g}"))
    ax.yaxis.set_minor_formatter(mticker.NullFormatter())
    ax.set_ylabel("Slowdown\nOver Best")
    ax.set_xticks([i + 1 for i in range(len(names))], names, fontsize=12)

    Path(get_figure_path()).mkdir(parents=True, exist_ok=True)
    plt.savefig(f"{get_figure_path()}/{name}.{get_figure_format()}", bbox_inches="tight")


def partition_skew_chart():
    name = "partition_skew"
    names, skew_factors, data = get_benchmarks(name, x_axis="skewFactor")

    fig, ax = plt.subplots(figsize=(6, 2.5))
    markers = ["o", "s", "^", "D", "P", "X"]

    algorithm_map = {}
    for n, s, d in zip(names, skew_factors, data):
        algorithm_map.setdefault(n, []).append((s, geo_mean([1 / b.execution_time for b in d])))

    for marker, (n, points) in zip(markers, algorithm_map.items()):
        points = sorted(points, key=lambda p: float("-inf") if p[0] is None else p[0])
        ax.plot(
            [p[0] for p in points],
            [p[1] for p in points],
            label=n,
            marker=marker,
        )
    ax.legend(
        borderaxespad=0.2,
        borderpad=0.15,
        labelspacing=0.2,
        handletextpad=0.1,
        handlelength=1.0,
        # Anchor the legend just outside the right edge (x=1.02) and vertically centered (y=0.5).
        loc="center left",
        bbox_to_anchor=(1.02, 0.5),
    )
    ax.set_ylabel("Throughput\nin Queries/s\n(Geometric Mean)")
    ax.set_xlabel("Skew Factor")
    Path(get_figure_path()).mkdir(parents=True, exist_ok=True)
    plt.savefig(f"{get_figure_path()}/{name}.{get_figure_format()}", bbox_inches="tight")


def cardinality_error_chart(name: str):
    names, card_errs, data = get_benchmarks(name, x_axis="cardinalityError")
    print(names)
    print(card_errs)
    print(len(data))

    fig, ax = plt.subplots(figsize=(6, 2.5))
    markers = ["o", "s", "^", "D", "P", "X"]

    algorithm_map = {}
    for n, s, d in zip(names, card_errs, data):
        algorithm_map.setdefault(n, []).append((s, geo_mean([1 / b.execution_time for b in d])))

    for marker, (n, points) in zip(markers, algorithm_map.items()):
        points = sorted(points, key=lambda p: float("-inf") if p[0] is None else p[0])
        ax.plot(
            [p[0] for p in points],
            [p[1] for p in points],
            label=n,
            marker=marker,
        )

    ax.set_ylabel("Throughput\nin Queries/s\n(Geometric Mean)")
    ax.set_xlabel("Cardinality Error Factor")
    ax.set_xscale("log", base=10)
    # Cardinality estimation errors are computed like this:
    # min, max, n = 0, 2, 10
    # print("[" + ", ".join([f"{10 ** (min + (max - min) / n * i):.2f}" for i in range(n + 1)]) + "]")
    ax.set_ylim(0, None)
    ticks = [1, 10, 100]
    ax.set_xticks(ticks, [str(x) for x in ticks])
    ax.legend(
        borderaxespad=0.2,
        borderpad=0.15,
        labelspacing=0.2,
        handletextpad=0.1,
        handlelength=1.0,
        # Anchor the legend just outside the right edge (x=1.02) and vertically centered (y=0.5).
        loc="center left",
        bbox_to_anchor=(1.02, 0.5),
    )
    Path(get_figure_path()).mkdir(parents=True, exist_ok=True)
    plt.savefig(f"{get_figure_path()}/{name}.{get_figure_format()}", bbox_inches="tight")


def create_all_sim_figures():
    get_figure_path().mkdir(parents=True, exist_ok=True)
    state_sep_chart("state_sep")
    generic_bar_chart("common")
    generic_bar_chart("scale_out")
    generic_bar_chart("scale_out_to_large")
    generic_bar_chart("heterogeneous")
    generic_bar_chart_by_workload("common")
    generic_bar_chart_by_workload("scale_out")
    generic_bar_chart_by_workload("scale_out_to_large")
    generic_bar_chart_by_workload("heterogeneous")
    generic_bar_chart("growing_scale_out_to_large")
    partition_scaling_chart()
    node_scaling_chart()
    full_enumeration_chart()
    partition_skew_chart()
    cardinality_error_chart("cardinality_errors")
    cardinality_error_chart("cardinality_errors_common")


def main():
    load_all_problem_files()

    # These settings are only required for producing figures for the paper
    set_use_latex(True)
    setup_matplotlib_latex_font()

    configure_cmake()
    compile_all()
    compile_all_debug()
    # run_all_benchmarks()
    # create_all_sim_figures()

    # plot_timeline("timeline")

    # run_benchmark(Path(f"{get_benchmark_definition_path()}/state_sep.json"))
    # state_sep_chart("state_sep")

    # Takes about 1 minute to run
    # run_benchmark(Path(f"{get_benchmark_definition_path()}/common.json"))
    # generic_bar_chart("common")
    # generic_bar_chart_by_workload("common")

    # run_benchmark(Path(f"{get_benchmark_definition_path()}/scale_out.json"))
    # run_benchmark(Path(f"{get_benchmark_definition_path()}/scale_out_to_large.json"))
    # generic_bar_chart("scale_out")
    # generic_bar_chart_by_workload("scale_out")
    # generic_bar_chart("scale_out_to_large")
    # generic_bar_chart_by_workload("scale_out_to_large")

    # run_benchmark(Path(f"{get_benchmark_definition_path()}/heterogeneous.json"))
    # generic_bar_chart("heterogeneous")
    # generic_bar_chart_by_workload("heterogeneous")

    # This takes ~4 minutes to run
    # run_benchmark(Path(f"{get_benchmark_definition_path()}/growing_scale_out_to_large.json"))
    # generic_bar_chart("growing_scale_out_to_large")

    # run_benchmark(Path(f"{get_benchmark_definition_path()}/scaling_partitions.json"))
    # run_benchmark(Path(f"{get_benchmark_definition_path()}/scaling_partitions_heft.json"))
    # run_benchmark(Path(f"{get_benchmark_definition_path()}/scaling_partitions_random.json"))
    # partition_scaling_chart()

    # This takes >30 minutes to run
    # run_benchmark(Path(f"{get_benchmark_definition_path()}/scaling_nodes.json"))
    # This takes >60 minutes to run
    # run_benchmark(Path(f"{get_benchmark_definition_path()}/scaling_nodes_random.json"))
    # node_scaling_chart()

    # This takes ~30 minutes to run
    # run_benchmark(Path(f"{get_benchmark_definition_path()}/full_enumeration.json"))
    # full_enumeration_chart()

    # run_benchmark(Path(f"{get_benchmark_definition_path()}/partition_skew.json"))
    # partition_skew_chart()

    # This takes about 10 minutes to run
    # run_benchmark(Path(f"{get_benchmark_definition_path()}/cardinality_errors.json"))
    # cardinality_error_chart("cardinality_errors")
    # run_benchmark(Path(f"{get_benchmark_definition_path()}/cardinality_errors_common.json"))
    # cardinality_error_chart("cardinality_errors_common")
    # run_benchmark(Path(f"{get_benchmark_definition_path()}/cardinality_errors_scale_out.json"))
    # cardinality_error_chart("cardinality_errors_scale_out")
    # run_benchmark(Path(f"{get_benchmark_definition_path()}/cardinality_errors_heterogeneous.json"))
    # cardinality_error_chart("cardinality_errors_heterogeneous")


if __name__ == "__main__":
    main()
