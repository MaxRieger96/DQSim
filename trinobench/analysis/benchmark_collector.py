import re
from pathlib import Path
from typing import Any, Callable

from pydantic.dataclasses import dataclass

from trinobench.analysis.infra import parallel_map
from trinobench.analysis.trino_reading import Query, load_json_file, load_query


@dataclass
class BenchmarkDescription:
    benchmark_name: str
    query_name: str
    n_nodes: int
    n_cores_per_node: int
    bandwidth: str  # raw string, e.g. "500mbit", "10gbit"
    bandwidth_gbit: float  # normalised to gbit/s, e.g. 0.5, 10.0
    scale_factor: int
    run_index: int
    path: Path
    query: Query


def parse_bandwidth_gbit(s: str) -> float:
    """Parse '500mbit' or '10gbit' into gbit/s as a float."""
    m = re.fullmatch(r"([0-9.]+)(mbit|gbit)", s)
    assert m, f"Unrecognised bandwidth format: {s!r}"
    value = float(m[1])
    return value / 1000 if m[2] == "mbit" else value


Predicate = Callable[[BenchmarkDescription], bool]


def benchmark_filter(benchmarks: list[BenchmarkDescription], *predicates: Predicate) -> list[BenchmarkDescription]:
    return [b for b in benchmarks if all(p(b) for p in predicates)]


def core_filter(n: int) -> Callable[[BenchmarkDescription], bool]:
    return lambda b: b.n_cores_per_node == n


def worker_filter(n: int) -> Callable[[BenchmarkDescription], bool]:
    return lambda b: b.n_nodes == n


def network_filter(n: str) -> Callable[[BenchmarkDescription], bool]:
    return lambda b: b.bandwidth == n


def scale_factor_filter(n: int) -> Callable[[BenchmarkDescription], bool]:
    return lambda b: b.scale_factor == n


def query_name_filter(n: str) -> Callable[[BenchmarkDescription], bool]:
    return lambda b: b.query_name == n


def benchmark_group_by(benchmarks: list, key: Callable) -> dict:
    result = {}
    for b in benchmarks:
        result.setdefault(key(b), []).append(b)
    return result


def _config_key(b: BenchmarkDescription) -> tuple:
    return (b.benchmark_name, b.query_name, b.n_nodes, b.n_cores_per_node, b.bandwidth, b.scale_factor)


def _load_benchmark_file(path: Path) -> BenchmarkDescription | None:
    """Parse and load a single benchmark file. Returns None if it should be skipped."""
    m = re.fullmatch(
        r"(?P<benchmark>[^_]+)_(?P<query>q?\d+)_(?P<nodes>\d+)w_(?P<cores>\d+)c_(?P<bw>\d+(?:mbit|gbit))_sf(?P<sf>\d+)_r(?P<run>\d+)",
        path.stem,
    )
    if not m:
        print(f"could not match path {path}")
        return None
    bw = m["bw"]
    return BenchmarkDescription(
        benchmark_name=m["benchmark"],
        query_name=m["query"],
        n_nodes=int(m["nodes"]),
        n_cores_per_node=int(m["cores"]),
        bandwidth=bw,
        bandwidth_gbit=parse_bandwidth_gbit(bw),
        scale_factor=int(m["sf"]),
        run_index=int(m["run"]),
        path=path,
        query=load_query(load_json_file(path)),  # the slow part
    )


def load_benchmarks(*dirs: Path, parallelism) -> list[BenchmarkDescription]:
    """Load benchmarks from directories, keeping only the fastest repeat run per configuration."""
    all_paths = [path for d in dirs for path in d.glob("*.json")]

    loaded = parallel_map(_load_benchmark_file, all_paths, workers=parallelism, label="benchmark files")
    # parallel_map already drops failures; drop regex non-matches too
    benchmarks: dict[Any, list[BenchmarkDescription]] = {}
    for b in loaded:
        if b is None:
            continue
        benchmarks.setdefault(_config_key(b), []).append(b)

    result = []
    for b, lst in benchmarks.items():
        lst = sorted(lst, key=lambda b: b.query.execution_time)
        if len(lst) < 5:
            print(f"could only find {len(lst)} results for {b}")
            continue
        result.append(lst[1])
    return result
