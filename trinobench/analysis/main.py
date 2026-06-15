from pathlib import Path
from typing import Callable

from trinobench.analysis.benchmark_collector import (
    BenchmarkDescription,
    benchmark_filter,
    core_filter,
    load_benchmarks,
    network_filter,
    query_name_filter,
    scale_factor_filter,
    worker_filter,
)
from trinobench.analysis.infra import q_error
from trinobench.analysis.metrics import compute_all_metrics
from trinobench.analysis.plots import (
    plot_bandwidth,
    plot_cores,
    plot_workers,
)
from visualization.infra import compile_all, compile_all_debug


def main():
    compile_all()
    compile_all_debug()

    def five_gbit(b: BenchmarkDescription) -> bool:
        return b.bandwidth == "5gbit"

    def sf10_1gbit(b: BenchmarkDescription) -> bool:
        return b.scale_factor == 10 and b.bandwidth == "1gbit"

    def some_debugging_query(b: BenchmarkDescription) -> bool:
        return b.query_name == "10" and b.scale_factor == 10 and b.n_nodes == 1 and b.n_cores_per_node == 2

    # set to 1 for debugging and to None for best performance
    parallelism = None

    workload = "tpch"
    # workload = "test"
    current_benchmarks = load_benchmarks(Path(f"trinobench/results/{workload}"), parallelism=parallelism)
    figure_dir = "trinobench/figures"

    bandwidth_list = benchmark_filter(
        current_benchmarks,
        core_filter(4),
        worker_filter(3),
        scale_factor_filter(100),
        network_filter("500mbit"),
        query_name_filter("8"),
    )
    metrics = compute_all_metrics(bandwidth_list, parallelism)
    sorted_by_q_err = sorted(metrics, key=lambda m: q_error(m.measured_time, m.simulated_time))
    i = -1
    print(
        f"Query {sorted_by_q_err[i].benchmark.path} has q-error of {q_error(sorted_by_q_err[i].measured_time, sorted_by_q_err[i].simulated_time):.2f}\n simulated: {sorted_by_q_err[i].simulated_time:.2f}s, measured: {sorted_by_q_err[i].measured_time:.2f}s, cpu: {sorted_by_q_err[i].cpu_time:.2f}s, network: {sorted_by_q_err[i].network_time:.2f}s"
    )
    # test_set = benchmark_filter(current_benchmarks, single_worker, single_core)
    # print(f"test set has {len(test_set)} benchmarks")
    # metrics = compute_all_metrics(test_set, parallelism)
    # plot_bandwidth(metrics, Path(f"{figure_dir}/execution_times_1w1c_bandwidth_{workload}.pdf"))

    # test_set = benchmark_filter(current_benchmarks, single_worker, five_gbit)
    # print(f"test set has {len(test_set)} benchmarks")
    # metrics = compute_all_metrics(test_set, parallelism)
    # plot_cores(metrics, Path(f"{figure_dir}/execution_times_1w5gbit_cores_{workload}.pdf"))

    # test_set = benchmark_filter(current_benchmarks, single_core, ten_gbit)
    # print(f"test set has {len(test_set)} benchmarks")
    # metrics = compute_all_metrics(test_set, parallelism)
    # plot_workers(metrics, Path(f"{figure_dir}/execution_times_1c10gbit_workers_{workload}.pdf"))

    # test_set = benchmark_filter(current_benchmarks, triple_core, ten_gbit)
    # print()
    # for q in test_set:
    #     print(q.path)
    # print()
    # print(f"test set has {len(test_set)} benchmarks")
    # metrics = compute_all_metrics(test_set, parallelism)
    # plot_workers(metrics, Path(f"{figure_dir}/execution_times_3c10gbit_workers_{workload}.pdf"))

    # test_set = benchmark_filter(current_benchmarks, some_debugging_query)
    # print()
    # for q in test_set:
    #     print(q.path)
    # print()
    # print(f"test set has {len(test_set)} benchmarks")
    # metrics = compute_all_metrics(test_set, parallelism)
    # exit(0)

    test_set = benchmark_filter(current_benchmarks, core_filter(2), five_gbit)
    print()
    for q in test_set:
        print(q.path)
    print()
    print(f"test set has {len(test_set)} benchmarks")
    metrics = compute_all_metrics(test_set, parallelism)
    plot_workers(metrics, Path(f"{figure_dir}/execution_times_2c5gbit_workers_{workload}.pdf"))


if __name__ == "__main__":
    main()
