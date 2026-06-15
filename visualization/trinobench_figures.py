from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
from typing_extensions import Callable

from trinobench.analysis.benchmark_collector import (
    BenchmarkDescription,
    benchmark_filter,
    core_filter,
    load_benchmarks,
    network_filter,
    scale_factor_filter,
    worker_filter,
)
from trinobench.analysis.infra import q_error
from trinobench.analysis.metrics import compute_all_metrics
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


def log_offsets(names: list[str], offset_range: float = 0.4) -> dict[str, float]:
    if len(names) == 1:
        return {names[0]: 1.0}
    return {
        name: 10**off
        for name, off in zip(
            names,
            [-offset_range / 2 + offset_range * i / (len(names) - 1) for i in range(len(names))],
        )
    }


def linear_offsets(names: list[str], offset_range: float = 0.4) -> dict[str, float]:
    if len(names) == 1:
        return {names[0]: 0.0}
    return {name: -offset_range / 2 + offset_range * i / (len(names) - 1) for i, name in enumerate(names)}


def add_violin_pair(ax, xs, left_data, right_data, xlabel: str):
    cycler = iter(plt.rcParams["axes.prop_cycle"])
    left_style, right_style = next(cycler), next(cycler)

    log_xs = np.log10(xs)
    left_pos, right_pos = log_xs - 0.18, log_xs + 0.18

    left_violins = ax.violinplot(left_data, positions=left_pos, widths=0.30)
    right_violins = ax.violinplot(right_data, positions=right_pos, widths=0.30)

    for violins, style in [(left_violins, left_style), (right_violins, right_style)]:
        for body in violins["bodies"]:
            body.set_facecolor(style["color"])
            body.set_edgecolor(style["color"])
            body.set_alpha(0.7)

    ax.scatter([], [], label="Measured", marker="o", color=left_style["color"])
    ax.scatter([], [], label="Simulated", marker="s", color=right_style["color"])
    ax.set_xlabel(xlabel)
    ax.set_ylabel("Time in s")
    ax.set_yscale("log")
    ax.set_xticks(log_xs, xs)
    ax.legend(
        borderaxespad=0.2,
        borderpad=0.15,
        labelspacing=0.2,
        handletextpad=0.1,
        handlelength=1.0,
    )

    return left_style, right_style


def plot_scale_factors(benchmark_list: list[BenchmarkDescription]):
    benchs = benchmark_filter(benchmark_list, core_filter(4), worker_filter(3), network_filter("5gbit"))
    print(f"scale factor plot has {len(benchs)} benchmarks")

    metrics = compute_all_metrics(benchs, None)
    benchmarks = sorted({m.benchmark.query_name for m in metrics})
    offsets = log_offsets(benchmarks)

    q_errors = defaultdict(list)
    measured_times = defaultdict(list)
    simulated_times = defaultdict(list)
    q_errors_by_x = defaultdict(list)

    for m in metrics:
        sf = m.benchmark.scale_factor
        qe = q_error(m.simulated_time, m.measured_time)
        q_errors[m.benchmark.query_name].append((sf * offsets[m.benchmark.query_name], qe))
        measured_times[sf].append(m.measured_time)
        simulated_times[sf].append(m.simulated_time)
        q_errors_by_x[sf].append(qe)

    for x in sorted(q_errors_by_x):
        print(f"sf={x}: mean q-error = {np.mean(q_errors_by_x[x]):.3f}")
    scale_factors = sorted(measured_times)
    fig, (ax_time, ax_qerr) = plt.subplots(1, 2, figsize=(6, 2), constrained_layout=True)

    measured_style, _ = add_violin_pair(
        ax_time,
        scale_factors,
        [measured_times[sf] for sf in scale_factors],
        [simulated_times[sf] for sf in scale_factors],
        "Scale factor",
    )

    for benchmark in benchmarks:
        xs, ys = zip(*q_errors[benchmark])
        ax_qerr.scatter(xs, ys, alpha=0.7, marker="o", **measured_style)

    ax_qerr.set_xlabel("Scale factor")
    ax_qerr.set_ylabel("Q-Error")
    ax_qerr.set_xscale("log")
    ax_qerr.set_xticks(scale_factors, scale_factors)

    fig.savefig(Path(f"{get_figure_path()}/trino_scale_factors.{get_figure_format()}"), bbox_inches="tight")
    plt.close(fig)


def plot_dimension(
    benchmark_list: list[BenchmarkDescription],
    filters: tuple[Callable[[BenchmarkDescription], bool], ...],
    value_of: Callable[[BenchmarkDescription], int | float],
    xlabel: str,
    filename: str,
    x_log: bool,
    y_log: bool,
):
    benchs = benchmark_filter(benchmark_list, *filters)
    print(f"{xlabel.lower()} plot has {len(benchs)} benchmarks")

    metrics = compute_all_metrics(benchs, None)
    benchmarks = sorted({m.benchmark.query_name for m in metrics})
    if x_log:
        offsets = log_offsets(benchmarks, 0.125)
    else:
        offsets = linear_offsets(benchmarks)

    q_errors = defaultdict(list)
    measured_times = defaultdict(list)
    simulated_times = defaultdict(list)
    q_errors_by_x = defaultdict(list)

    for m in metrics:
        x = value_of(m.benchmark)
        qe = q_error(m.simulated_time, m.measured_time)
        if x_log:
            q_errors[m.benchmark.query_name].append((x * offsets[m.benchmark.query_name], qe))
        else:
            q_errors[m.benchmark.query_name].append((x + offsets[m.benchmark.query_name], qe))
        measured_times[x].append(m.measured_time)
        simulated_times[x].append(m.simulated_time)
        q_errors_by_x[x].append(qe)

    for x in sorted(q_errors_by_x):
        print(f"{xlabel}={x}: mean q-error = {np.mean(q_errors_by_x[x]):.3f}")

    xs = sorted(measured_times)
    fig, (ax_qerr, ax_time) = plt.subplots(2, 1, figsize=(6.0, 2.8), sharex=True, constrained_layout=True)

    measured_style = next(iter(plt.rcParams["axes.prop_cycle"]))

    for benchmark in benchmarks:
        qerr_xs, qerr_ys = zip(*q_errors[benchmark])
        ax_qerr.scatter(qerr_xs, qerr_ys, alpha=0.7, marker="o", **measured_style)

    ax_qerr.set_ylabel("Q-Error")

    ax_time.plot(xs, [np.average(measured_times[x]) for x in xs], marker="o", label="Measured")
    ax_time.plot(xs, [np.average(simulated_times[x]) for x in xs], marker="s", label="Simulated")
    ax_time.set_xlabel(xlabel)
    ax_time.set_ylabel("Time in s")
    if x_log:
        ax_time.set_xscale("log")
    if y_log:
        ax_time.set_yscale("log")
    else:
        ax_time.set_ylim(0)
    ax_time.set_xticks(xs, xs)
    ax_time.legend(
        borderaxespad=0.2,
        borderpad=0.15,
        labelspacing=0.2,
        handletextpad=0.1,
        handlelength=1.0,
    )

    fig.savefig(Path(f"{get_figure_path()}/{filename}.{get_figure_format()}"), bbox_inches="tight")
    plt.close(fig)


def plot_workers(benchmark_list: list[BenchmarkDescription]):
    plot_dimension(
        benchmark_list,
        (core_filter(2), network_filter("5gbit"), scale_factor_filter(100)),
        lambda b: b.n_nodes,
        "Workers",
        "trino_workers",
        False,
        False,
    )


def plot_cores(benchmark_list: list[BenchmarkDescription]):
    plot_dimension(
        benchmark_list,
        (worker_filter(3), network_filter("5gbit"), scale_factor_filter(100)),
        lambda b: b.n_cores_per_node,
        "Cores",
        "trino_cores",
        False,
        False,
    )


def plot_bandwidth(benchmark_list: list[BenchmarkDescription]):
    plot_dimension(
        benchmark_list,
        (core_filter(4), worker_filter(3), scale_factor_filter(100)),
        lambda b: b.bandwidth_gbit,
        "Network bandwidth in Gbit/s",
        "trino_network_bandwidth",
        True,
        False,
    )


def get_trino_benchmark_results():
    download_artifact("tpch.zip")
    extract_zip("downloaded_data/tpch.zip", "tpch", "trinobench/results/tpch")


def trino_bench_figures():
    workload = "tpch"
    benchmarks = load_benchmarks(Path(f"trinobench/results/{workload}"), parallelism=None)

    get_figure_path().mkdir(parents=True, exist_ok=True)
    plot_bandwidth(benchmarks)
    plot_scale_factors(benchmarks)
    plot_workers(benchmarks)
    plot_cores(benchmarks)


def main():
    configure_cmake()
    compile_all_debug()
    compile_all()

    set_use_latex(True)
    setup_matplotlib_latex_font()

    trino_bench_figures()


if __name__ == "__main__":
    main()
