from pathlib import Path

import matplotlib.pyplot as plt

from trinobench.analysis.benchmark_collector import (
    BenchmarkDescription,
    benchmark_group_by,
)
from trinobench.analysis.infra import q_error
from trinobench.analysis.metrics import BenchmarkMetrics


def plot_execution_time_vs_bandwidth(benchmarks: list[BenchmarkDescription]):
    by_sf = benchmark_group_by(benchmarks, lambda b: b.scale_factor)
    query_names = sorted({b.query_name for b in benchmarks})
    offsets = {q: 1 + (len(query_names) - i) / 20 for i, q in enumerate(query_names)}
    print(offsets)

    fig, ax = plt.subplots()
    for sf, group in sorted(by_sf.items()):
        ax.scatter(
            [b.bandwidth_gbit * offsets[b.query_name] for b in group],
            [b.query.execution_time for b in group],
            label=f"sf={sf}",
        )

    ax.set_xlabel("Bandwidth (gbit/s)")
    ax.set_ylabel("Execution time (s)")
    ax.set_xscale("log")
    ax.legend()
    fig.savefig("execution_times.pdf")


def _plot_vs_x(
    metrics: list[BenchmarkMetrics],
    output_path: Path,
    x_accessor,
    x_label: str,
):
    output_path.parent.mkdir(parents=True, exist_ok=True)
    series = [
        ("measured", lambda m: m.measured_time),
        ("simulated", lambda m: m.simulated_time),
        ("cpu + net", lambda m: m.cpu_time + m.network_time),
    ]
    by_query = benchmark_group_by(metrics, lambda m: m.benchmark.query_name)
    query_names = sorted(by_query.keys())
    fig, axes = plt.subplots(len(query_names), 1, figsize=(6, 4 * len(query_names)), sharex=True)
    if len(query_names) == 1:
        axes = [axes]
    scale_factors = sorted({m.benchmark.scale_factor for m in metrics})
    colors = {sf: plt.cm.tab10(i) for i, sf in enumerate(scale_factors)}
    offsets = {label: (i - len(series) / 2) * 0.03 for i, (label, _) in enumerate(series)}
    markers = ["o", "s", "^", "D", "v", "P"]
    for ax, query_name in zip(axes, query_names):
        query_metrics = by_query[query_name]
        by_sf = benchmark_group_by(query_metrics, lambda m: m.benchmark.scale_factor)
        for i, (label, accessor) in enumerate(series):
            for sf, sf_group in sorted(by_sf.items()):
                ax.scatter(
                    [x_accessor(m) * (1 + offsets[label]) for m in sf_group],
                    [accessor(m) for m in sf_group],
                    label=f"sf={sf} {label}" if query_name == query_names[0] else None,
                    color=colors[sf],
                    marker=markers[i % len(markers)],
                    alpha=(1.0 if i < 2 else 0.5),
                )

        for m in query_metrics:
            measured_x = x_accessor(m) * (1 + offsets["measured"])
            simulated_x = x_accessor(m) * (1 + offsets["simulated"])
            measured_y = m.measured_time
            simulated_y = m.simulated_time
            q_error_val = q_error(measured_y, simulated_y)

            ax.text(
                (measured_x * simulated_x) ** 0.5,
                (measured_y * simulated_y) ** 0.5,
                f"{q_error_val:.2f}",
                fontsize=8,
                ha="center",
                va="center",
            )

        ax.set_title(query_name)
        ax.set_ylabel("Execution time (s)")
        ax.set_xscale("log")
        ax.set_yscale("log")
    axes[-1].set_xlabel(x_label)
    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(
        handles, labels, loc="lower center", bbox_to_anchor=(0.5, -0.1), ncol=len(scale_factors), borderaxespad=0
    )
    fig.tight_layout()
    fig.savefig(output_path, bbox_inches="tight")


def plot_bandwidth(metrics: list[BenchmarkMetrics], output_path: Path):
    _plot_vs_x(metrics, output_path, lambda m: m.benchmark.bandwidth_gbit, "Bandwidth (gbit/s)")


def plot_cores(metrics: list[BenchmarkMetrics], output_path: Path):
    _plot_vs_x(
        metrics,
        output_path,
        lambda m: m.benchmark.n_nodes * m.benchmark.n_cores_per_node,
        "Total cores (nodes × cores/node)",
    )


def plot_workers(metrics: list[BenchmarkMetrics], output_path: Path):
    _plot_vs_x(metrics, output_path, lambda m: m.benchmark.n_nodes, "Number of workers (nodes)")
