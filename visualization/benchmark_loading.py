import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Optional

VARIANT_ORDER = ("skewFactor", "cardinalityError")
VariantName = str
VariantValue = Optional[float]


@dataclass
class SimulationBench:
    name: str
    algorithm: str
    execution_time: float
    scheduling_time: float
    query_name: str
    workload: str
    handcrafted: bool
    caching_name: str
    cluster_name: str
    n_nodes: int
    homogeneous: bool
    n_partitions: int
    skew_factor: Optional[float]
    cardinality_error: Optional[float]

    def is_shared_nothing(self) -> bool:
        return (
            (not self.handcrafted)
            and (self.n_nodes == self.n_partitions)
            and self.caching_name == "All"
            and self.homogeneous
        )


@dataclass(frozen=True, order=True)
class BenchmarkVariant:
    """The variable benchmark dimensions belonging to one algorithm run.

    The field order is intentional: filenames encode skew first, then cardinality
    error. Sorting and duplicate detection use the same order.
    """

    values: tuple[VariantValue, ...]
    fields: tuple[VariantName, ...] = VARIANT_ORDER

    @classmethod
    def from_raw(cls, raw: dict, fields: Iterable[VariantName] = VARIANT_ORDER) -> "BenchmarkVariant":
        fields = tuple(fields)
        return cls(tuple(optional_float(raw.get(field)) for field in fields), fields)

    def get(self, field: VariantName) -> VariantValue:
        return self.values[self.fields.index(field)]

    def has_field(self, field: VariantName) -> bool:
        return field in self.fields

    def sort_key(self) -> tuple[float, ...]:
        return tuple(none_first(value) for value in self.values)


@dataclass(frozen=True)
class BenchmarkIdentifier:
    algorithm: str
    variant: BenchmarkVariant


@dataclass
class BenchmarkRun:
    """Raw benchmark file plus parsed rows and its stable identity."""

    raw: dict
    variant_fields: tuple[VariantName, ...]

    @property
    def algorithm(self) -> str:
        return self.raw["assignmentAlgorithm"]

    @property
    def created_at(self) -> str:
        return self.raw["file_creation_time"]

    @property
    def variant(self) -> BenchmarkVariant:
        return BenchmarkVariant.from_raw(self.raw, self.variant_fields)

    @property
    def identifier(self) -> BenchmarkIdentifier:
        return BenchmarkIdentifier(self.algorithm, self.variant)

    def parse(self) -> list[SimulationBench]:
        return parse_benchmarks(self.raw)


@dataclass
class BenchmarkSet:
    """A loaded benchmark folder with ordering and duplicate policy in one place."""

    runs: list[BenchmarkRun]
    variant_fields: tuple[VariantName, ...]

    @classmethod
    def from_raw(
        cls,
        benchs: list[dict],
        variant_fields: Iterable[VariantName] | None = None,
    ) -> "BenchmarkSet":
        fields = tuple(variant_fields) if variant_fields is not None else infer_variant_fields(benchs)
        return cls([BenchmarkRun(b, fields) for b in benchs], fields)

    def newest_per_identifier(self) -> "BenchmarkSet":
        newest: dict[BenchmarkIdentifier, BenchmarkRun] = {}
        for run in self.runs:
            old = newest.get(run.identifier)
            if old is None or run.created_at > old.created_at:
                newest[run.identifier] = run
        if len(newest) != len(self.runs):
            print_duplicate_warning(self.variant_fields)
        return BenchmarkSet(list(newest.values()), self.variant_fields)

    def ordered(self) -> "BenchmarkSet":
        name_map = get_new_name_map()
        for run in self.runs:
            if run.algorithm not in name_map:
                print(f"Found algorithm {run.algorithm} but did not find a new name for it")
                name_map[run.algorithm] = run.algorithm

        name_order = {old_name: i for i, old_name in enumerate(name_map)}
        ordered_runs = sorted(
            self.runs,
            key=lambda run: (name_order[run.algorithm], run.variant.sort_key()),
        )
        return BenchmarkSet(ordered_runs, self.variant_fields)

    def validate(self, benchmark_name: str) -> None:
        identifiers = [run.identifier for run in self.runs]
        assert len(set(identifiers)) == len(identifiers), (
            f"There are duplicate benchmark identifiers in {benchmark_name}"
        )

        parsed = [run.parse() for run in self.runs]
        if not parsed:
            return
        expected_count = len(parsed[0])
        for alg in parsed:
            assert len(alg) == expected_count, (
                f"Different algorithms have different benchmark counts {len(alg)} != {expected_count}"
            )

        # print the longest execution times and their query names
        # for run in self.runs:
        #     print(f"{benchmark_name} {run.algorithm}")
        #     benchs = run.parse()
        #     benchs = sorted(benchs, key=lambda b: b.execution_time, reverse=True)
        #     results = [f"{b.query_name} ({b.execution_time:.2f}s)" for b in benchs[:20]]
        #     print("\n".join(results))

    def as_legacy_tuple(
        self,
        x_axis: VariantName | None = None,
    ) -> tuple[list[str], list[VariantValue], list[list[SimulationBench]]]:
        axis = x_axis or self.default_x_axis()
        name_map = get_new_name_map()
        return (
            [name_map.get(run.algorithm, run.algorithm) for run in self.runs],
            [run.variant.get(axis) if run.variant.has_field(axis) else None for run in self.runs],
            [run.parse() for run in self.runs],
        )

    def default_x_axis(self) -> VariantName:
        if len(self.variant_fields) == 1:
            return self.variant_fields[0]
        return self.variant_fields[0] if self.variant_fields else "skewFactor"


def optional_float(value) -> Optional[float]:
    return float(value) if value is not None else None


def none_first(value: Optional[float]) -> float:
    return float("-inf") if value is None else value


def infer_variant_fields(benchs: list[dict]) -> tuple[VariantName, ...]:
    fields = tuple(field for field in VARIANT_ORDER if any(b.get(field) is not None for b in benchs))
    return fields or ("skewFactor",)


def print_duplicate_warning(variant_fields: tuple[VariantName, ...]) -> None:
    fields = ", ".join(["assignmentAlgorithm", *variant_fields])
    print(
        f"Warning: Found multiple benchmarks with the same {fields}. "
        "Did you benchmark repeatedly?\n"
        "Choosing the newest one per group.\n"
        "You can clear them in the folder benchmark_results."
    )


def get_new_name_map():
    return {
        "AlignedRR": "RR",
        "Greedy1": "GMN",
        "HEFT": "HEFT",
        "NetHEFT": "NetHEFT",
        "Component": "cCEFT",
        "Rand_x1000": "Rand1k",
        "BruteForce": "Brute\nForce",
        "StateSep": "Full",
        "StateSep+B": "Full+B",
        "StateSepP2P": "P2P",
        "StateSepP2P+B": "P2P+B",
        "StateSepNoUpD": "NoUpD",
        "StateSepNoUpD+B": "NoUpD+B",
        "StateSepNoUp": "NoUp",
        "StateSepNoUp+B": "NoUp+B",
    }


def extract_timestamp(path: Path) -> str:
    """
    Extracts the ISO-8601 timestamp ending with 'Z' from a filename.
    Does not assume anything about the filename prefix or fractional-second length.
    """
    pattern = re.compile(r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?Z")
    match = pattern.search(path.name)
    if not match:
        raise ValueError(f"No timestamp found in filename: {path.name}")
    return match.group(0)


def get_data(folder: str):
    result = []
    for file in Path(f"benchmark_results/{folder}").iterdir():
        if file.is_file() and file.suffix == ".json":
            with file.open("r") as fd:
                current = json.load(fd)
                current["file_creation_time"] = extract_timestamp(file)
                result.append(current)
    return sorted(result, key=lambda x: x["assignmentAlgorithm"])


def get_cluster_size(cluster_name: str) -> int:
    if cluster_name == "handcrafted":
        return -1
    assert cluster_name[-1].isdigit()
    i_last_digit = len(cluster_name) - 1
    while cluster_name[i_last_digit].isdigit():
        i_last_digit -= 1
    assert cluster_name[i_last_digit] == "x"
    i_last_digit += 1
    return int(cluster_name[i_last_digit:])


def get_cluster_homogeneous(cluster_name: str) -> bool:
    if cluster_name == "handcrafted":
        return True
    assert cluster_name[-1].isdigit()
    assert cluster_name[-1].isdigit()
    i_last_digit = len(cluster_name) - 1
    while cluster_name[i_last_digit].isdigit():
        i_last_digit -= 1
    assert cluster_name[i_last_digit] == "x"
    node_name = cluster_name[:i_last_digit]
    return "-" in node_name


def parse_benchmarks(bench: dict) -> list[SimulationBench]:
    result = []
    skew_factor = optional_float(bench.get("skewFactor"))
    cardinality_error = optional_float(bench.get("cardinalityError"))
    for n, t, s in zip(bench["benchmarkNames"], bench["executionTimes"], bench["schedulingTimes"]):
        handcrafted = n.startswith("Handcrafted_")
        caching_name = "handcrafted"
        cluster_name = "handcrafted"
        w = "handcrafted"
        query_name = n.split("_")[-1]
        n_nodes = -1
        homogeneous = True
        n_partitions = -1
        if not handcrafted:
            if len(n.split("_")) == 4:
                cl, ca, p, q = n.split("_")
                w = "unnamed_workload"
            else:
                assert len(n.split("_")) == 5
                cl, ca, w, p, q = n.split("_")
            cluster_name = cl
            caching_name = ca
            n_nodes = get_cluster_size(cl)
            homogeneous = get_cluster_homogeneous(cl)
            n_partitions = int(p)
        result.append(
            SimulationBench(
                n,
                bench["assignmentAlgorithm"],
                t,
                s,
                query_name,
                w,
                handcrafted,
                caching_name,
                cluster_name,
                n_nodes,
                homogeneous,
                n_partitions,
                skew_factor,
                cardinality_error,
            )
        )
    return result


def load_benchmark_set(
    benchmark_name: str,
    variant_fields: Iterable[VariantName] | None = None,
) -> BenchmarkSet:
    return BenchmarkSet.from_raw(get_data(benchmark_name), variant_fields).newest_per_identifier().ordered()


def get_most_recent_benchmarks(
    benchs,
    variant_fields: Iterable[VariantName] | None = None,
    x_axis: VariantName | None = None,
) -> tuple[list[str], list[VariantValue], list[list[SimulationBench]]]:
    """Compatibility wrapper used by older callers and tests."""
    return BenchmarkSet.from_raw(benchs, variant_fields).newest_per_identifier().as_legacy_tuple(x_axis)


def get_benchmarks(
    benchmark_name: str,
    x_axis: VariantName | None = None,
    variant_fields: Iterable[VariantName] | None = None,
) -> tuple[list[str], list[Optional[float]], list[list[SimulationBench]]]:
    benchmarks = load_benchmark_set(benchmark_name, variant_fields)
    benchmarks.validate(benchmark_name)
    return benchmarks.as_legacy_tuple(x_axis)


def merge_data(n_lists: list[list[str]], d_lists: list[list[list]]) -> tuple[list[str], list[list]]:
    merge_dict = {}
    for ns, ds in zip(n_lists, d_lists):
        for n, d in zip(ns, ds):
            if n not in merge_dict:
                merge_dict[n] = []
            merge_dict[n] += d
    new_names = list(x for x in merge_dict.keys())
    new_name_map = get_new_name_map()
    new_name_order = {n: i for i, n in enumerate(new_name_map.values())}
    new_names = sorted(new_names, key=lambda n: new_name_order[n])
    new_data = [merge_dict[n] for n in new_names]
    return new_names, new_data
