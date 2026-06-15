import traceback

from pydantic.dataclasses import dataclass

from visualization.util.dqsim import (
    AssignmentProblem,
    Cluster,
    Node,
    Partition,
    Partitioning,
    PartitionMap,
    PartitionMap2,
    SimulatorSettings,
)
from trinobench.analysis.benchmark_collector import (
    BenchmarkDescription,
)
from trinobench.analysis.dqsim_adaptors import create_assignment, run_simulator, run_task_builder
from trinobench.analysis.dqsim_mapping import convert_trino_plan
from trinobench.analysis.infra import parallel_map
from trinobench.analysis.trino_reading import DataInfo, Query


@dataclass
class ClusterConfig:
    n_nodes: int
    n_cores_per_node: int
    # in gbit/s
    network_speed: float


def predict_load_time(data: DataInfo, cluster: ClusterConfig):
    # The time it takes to load the base relation data from the storage service
    return data.n_bytes / cluster.n_nodes / (cluster.network_speed * 1e9 / 8)


def predict_cpu_time(query: Query, cluster: ClusterConfig) -> float:
    return sum(stage.cpu_time for stage in query.stages) / cluster.n_nodes / cluster.n_cores_per_node


def predict_network_time(query: Query, cluster: ClusterConfig) -> float:
    return sum(predict_load_time(p.base_rel_input, cluster) for s in query.stages for t in s.tasks for p in t.pipelines)


def get_trino_cluster(cluster_config: ClusterConfig) -> Cluster:
    # For the Trino benchmark, we applied the same network speed to the storage service as to all nodes
    latency = 0
    nodes = [
        Node(cluster_config.n_cores_per_node, 1e100, cluster_config.network_speed, latency)
        for _ in range(cluster_config.n_nodes)
    ] + [Node(0, 1e100, 1e10, latency)]
    # The bandwidth limit script actually only limits container downloads not uploads
    # Consequently multiple nodes can download faster from the storage node which is represented by the 1e10 gbit network
    return Cluster(nodes, True)


def get_cluster_config(bench: BenchmarkDescription) -> ClusterConfig:
    return ClusterConfig(bench.n_nodes, bench.n_cores_per_node, bench.bandwidth_gbit)


def get_initial_trino_partitioning(plan, cluster: Cluster):
    # we cache all partitions only on the storage node
    owning_nodes: dict[Partition, int] = {}
    caching_nodes: dict[Partition, list[int]] = {}
    for du in plan.dataUnits:
        # only select base scan dus
        if du.isIntermediate:
            continue
        for partition_id in range(du.partitionLayout.nPartitions):
            part = Partition(du.id, partition_id)
            owning_nodes[part] = len(cluster.nodes) - 1
            caching_nodes[part] = []

    owning_nodes_map = [PartitionMap(k, v) for k, v in owning_nodes.items()]
    caching_nodes_map = [PartitionMap2(k, v) for k, v in caching_nodes.items()]
    return Partitioning(owning_nodes_map, caching_nodes_map, len(cluster.nodes))


@dataclass
class BenchmarkMetrics:
    benchmark: BenchmarkDescription
    measured_time: float
    simulated_time: float
    cpu_time: float
    network_time: float

    @property
    def cpu_plus_network(self) -> float:
        return self.cpu_time + self.network_time

    @property
    def cpu_max_network(self) -> float:
        return max(self.cpu_time, self.network_time)


def compute_metrics(b: BenchmarkDescription) -> BenchmarkMetrics:
    # print(b.path)
    query = b.query
    cluster = get_cluster_config(b)
    cluster_json = get_trino_cluster(cluster)
    distributed_plan, slowdown_factors, plan_dot = convert_trino_plan(query)
    initial_partitioning = get_initial_trino_partitioning(distributed_plan, cluster_json)
    assignment_problem = AssignmentProblem(
        cluster_json,
        distributed_plan,
        [],
        [],
        initial_partitioning,
        str(b.path),
    )
    try:
        assignment_problem = run_task_builder(assignment_problem)
    except Exception as e:
        print(f"Failed to run task builder: {e}")
        print(plan_dot)
        raise e
    assignment = create_assignment(assignment_problem)
    settings = SimulatorSettings.get_default()
    settings.taskSlowdownFactors = slowdown_factors
    sim_time = run_simulator(assignment_problem, assignment, settings)
    return BenchmarkMetrics(
        benchmark=b,
        measured_time=query.execution_time,
        simulated_time=sim_time,
        cpu_time=predict_cpu_time(query, cluster),
        network_time=predict_network_time(query, cluster),
    )


def compute_all_metrics(benchmarks: list[BenchmarkDescription], parallelism) -> list[BenchmarkMetrics]:
    return parallel_map(compute_metrics, benchmarks, workers=parallelism, label="queries")
