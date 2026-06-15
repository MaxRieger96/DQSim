from visualization.figures import create_all_sim_figures, load_all_problem_files, run_all_benchmarks
from visualization.infra import (
    compile_all,
    compile_all_debug,
    configure_cmake,
    set_use_latex,
    setup_matplotlib_latex_font,
)
from visualization.trinobench_figures import get_trino_benchmark_results, trino_bench_figures


def main():
    # Compile DQSim
    configure_cmake()
    compile_all()
    compile_all_debug()

    # Configure matplotlib
    set_use_latex(True)
    setup_matplotlib_latex_font()

    # Simulator figures
    load_all_problem_files()
    run_all_benchmarks()
    create_all_sim_figures()

    # Trino benchmark figures
    get_trino_benchmark_results()
    trino_bench_figures()


if __name__ == "__main__":
    main()
