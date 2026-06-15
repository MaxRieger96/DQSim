import dataclasses
import json
from copy import deepcopy
from dataclasses import dataclass, field, fields, is_dataclass
from pathlib import Path
from typing import Any, Optional, TypeVar


@dataclass
class DU:
    id: int
    cardinality: float
    byte_size: float
    broadcast: bool
    single_node: bool
    is_base_relation: bool

    def increase_du_id(self, n: int) -> "DU":
        self.id += n
        return self

    def remap_dus(self, map: dict[int, int]) -> "DU":
        self.id = map.get(self.id, self.id)
        return self


@dataclass
class DUInfo:
    # maps from the id of a pipeline to the input DU id
    pipeline_inputs: dict[int, list[int]]
    # maps from the id of a pipeline to the output DU id
    pipeline_outputs: dict[int, int]


def asdict_with_json_names(obj: Any) -> Any:
    """
    Convert a dataclass instance (possibly nested) to a structure of
    dicts/lists/primitives, using `metadata["json_name"]` if present
    for each dataclass field.
    """
    if is_dataclass(obj):
        result = {}
        for f in fields(obj):
            value = getattr(obj, f.name)
            json_name = f.metadata.get("json_name", f.name)
            result[json_name] = asdict_with_json_names(value)
        return result
    elif isinstance(obj, (list, tuple)):
        return [asdict_with_json_names(v) for v in obj]
    elif isinstance(obj, dict):
        return {k: asdict_with_json_names(v) for k, v in obj.items()}
    else:
        return obj


@dataclass
class Attribute:
    name: str
    dataUnitId: int
    attributeIndex: int
    size: float


def create_attrs(du: DU) -> list[Attribute]:
    n_attrs = 1
    result = []
    for i in range(n_attrs):
        result.append(Attribute(f"attr_{i}", du.id, i, du.byte_size / n_attrs))
    return result


@dataclass
class PartitionLayout:
    type: str  # hashpartitioned, broadcast, singlenode
    nPartitions: int
    attributes: list[Attribute]

    @staticmethod
    def get_single_node_layout() -> "PartitionLayout":
        return PartitionLayout("singlenode", 1, [])


@dataclass
class FullDU:
    name: str
    attributes: list[Attribute]
    partitionLayout: PartitionLayout
    id: int
    estimatedCard: float
    isIntermediate: bool  # true if the DU is not a base relation


def createFullDU(du: DU, parallelism: int) -> FullDU:
    if du.broadcast:
        parallelism = 1
    attrs = create_attrs(du)
    partition_type = "hashpartitioned"
    if du.single_node:
        partition_type = "singlenode"
    elif du.broadcast:
        partition_type = "broadcast"
    assert not (du.single_node and du.broadcast), "a DU cannot be both!"
    n_partitions = parallelism
    part_layout = PartitionLayout(
        partition_type,
        n_partitions,
        [attrs[0]],
    )
    return FullDU(
        f"du_{du.id}",
        attrs,
        part_layout,
        du.id,
        du.cardinality,
        not du.is_base_relation,
    )


def find_du(id: int, dus: list[FullDU]) -> FullDU:
    for du in dus:
        if du.id == id:
            return du
    assert False, "could not find DU"


@dataclass
class FullPipeline:
    id: int
    scan: int
    output: int
    estimatedLoad: float
    requiredData: list[int]
    description: str


@dataclass
class Shuffle:
    from_: int = field(metadata={"json_name": "from"})
    to: int


@dataclass
class DistributedPlan:
    dataUnits: list[FullDU]
    pipelines: list[FullPipeline]
    shuffleStages: list[Shuffle]
    dependencyOrder: list[int]

    def _get_result_du(self) -> int:
        unused_dus = {du.id for du in self.dataUnits}
        for p in self.pipelines:
            unused_dus.discard(p.scan)
            for du in p.requiredData:
                unused_dus.discard(du)
        for s in self.shuffleStages:
            unused_dus.discard(s.from_)
        assert len(unused_dus) == 1
        return unused_dus.pop()

    def augment_final_shuffle_if_necessary(self):
        result_du_id = self._get_result_du()
        result_du = self.dataUnits[result_du_id]
        assert result_du.id == result_du_id
        assert result_du.isIntermediate
        if result_du.partitionLayout.type == "singlenode":
            return
        new_du = FullDU(
            "shuffled_result",
            deepcopy(result_du.attributes),
            PartitionLayout.get_single_node_layout(),
            result_du_id + 1,
            result_du.estimatedCard,
            result_du.isIntermediate,
        )
        self.dataUnits.append(new_du)
        self.shuffleStages.append(Shuffle(result_du_id, new_du.id))


@dataclass(frozen=True)
class Partition:
    dataUnitId: int
    partitionIndex: int


@dataclass
class PartitionMap:
    k: Partition
    v: int  # node id


@dataclass
class PartitionMap2:
    k: Partition
    v: list[int]  # node ids


@dataclass
class Partitioning:
    owningNode: list[PartitionMap]
    cachingNodes: list[PartitionMap2]
    nNodes: int


@dataclass
class Task:
    pipelineId: int
    scannedPartition: Partition
    requiredPartitions: list[Partition]
    outputPartition: Partition
    broadcast: bool


@dataclass
class TaskMap:
    k: int  # the pipeline id
    v: Task


@dataclass
class TaskMap2:
    k: int  # the pipeline id
    v: list[Task]


@dataclass
class Node:
    nCores: int
    memorySize: float  # in GB
    networkSpeed: float  # in gbit/s
    networkLatency: float  # in seconds


@dataclass
class Cluster:
    nodes: list[Node]
    storageService: bool


@dataclass
class AssignmentProblem:
    cluster: Cluster
    plan: DistributedPlan
    tasks: list[TaskMap2]
    broadcastTasks: list[TaskMap]
    initialPartitioning: Partitioning
    name: str


def get_du_pipeline(du: DU, du_info: DUInfo, pipeline_plan: dict[int, Any]) -> Any:
    for p, d in du_info.pipeline_outputs.items():
        if d == du.id:
            return pipeline_plan[p]

    for p, ds in du_info.pipeline_inputs.items():
        for d in ds:
            if d == du.id:
                return pipeline_plan[p]

    assert False, f"could not find pipeline that reads from or writes to du {du.id}"


def get_partition(du_id: int, dus: dict[int, DU], partition_id: int) -> Partition:
    # get the respective partition, but get partition 0 for broadcast DUs
    du: DU = dus[du_id]
    if du.broadcast:
        return Partition(du_id, 0)
    return Partition(du_id, partition_id)


def get_dq_plan(
    dus: dict[int, DU],
    du_info: DUInfo,
    shuffles: list[tuple[int, int]],
    pipeline_plan: dict[int, Any],
) -> tuple[DistributedPlan, list[TaskMap2], list[TaskMap]]:
    plan_dus: dict[int, FullDU] = {}
    for du in dus.values():
        # find associated pipeline
        involved_pipeline = get_du_pipeline(du, du_info, pipeline_plan)
        plan_dus[du.id] = createFullDU(du, involved_pipeline.parallelism)

    pipelines = []
    for p in pipeline_plan.values():
        # the load should be per tuple normalized to 16 cores, here every task is executed on a single core
        scan_du = du_info.pipeline_inputs[p.id][0]
        out_du = du_info.pipeline_outputs[p.id]

        n_input_tuples = dus[scan_du].cardinality
        load = p.execution_time / 16 / n_input_tuples

        pipelines.append(
            FullPipeline(
                id=p.id,
                scan=scan_du,
                output=out_du,
                estimatedLoad=load,
                requiredData=du_info.pipeline_inputs[p.id][1:],
                description=f"pipeline_{p.id}",
            )
        )
        plan_dus[scan_du].partitionLayout.nPartitions = p.parallelism
        plan_dus[out_du].partitionLayout.nPartitions = p.parallelism

    shuffle_stages = []
    for f, t in shuffles:
        shuffle_stages.append(Shuffle(f, t))

    dependency_order = sorted([p.id for p in pipelines])

    # get tasks
    tasks = {}  # maps from pipeline id to task
    broadcast_tasks = {}
    for p in pipeline_plan.values():
        scan_du = du_info.pipeline_inputs[p.id][0]
        dependency_dus = du_info.pipeline_inputs[p.id][1:]
        out_du = du_info.pipeline_outputs[p.id]
        if p.parallelism > 1:
            current_tasks = []
            for partition_id in range(p.parallelism):
                current_tasks.append(
                    Task(
                        p.id,
                        get_partition(scan_du, dus, partition_id),
                        [get_partition(du, dus, partition_id) for du in dependency_dus],
                        Partition(out_du, partition_id),
                        False,
                    )
                )
            tasks[p.id] = current_tasks
        else:
            broadcast_tasks[p.id] = Task(
                p.id,
                Partition(scan_du, 0),
                [Partition(du, 0) for du in dependency_dus],
                Partition(out_du, 0),
                False,
            )
    tasks = [TaskMap2(k, v) for k, v in tasks.items()]
    broadcast_tasks = [TaskMap(k, v) for k, v in broadcast_tasks.items()]

    plan = DistributedPlan(
        sorted([du for du in plan_dus.values()], key=lambda du: du.id),
        sorted(pipelines, key=lambda p: p.id),
        shuffle_stages,
        dependency_order,
    )

    return plan, tasks, broadcast_tasks


def get_initial_partitioning(plan: DistributedPlan, n_nodes: int) -> Partitioning:
    # we cache all partitions on all nodes
    owning_nodes: dict[Partition, int] = {}
    caching_nodes: dict[Partition, list[int]] = {}
    for du in plan.dataUnits:
        # only select base scan dus
        if du.isIntermediate:
            continue
        for partition_id in range(du.partitionLayout.nPartitions):
            part = Partition(du.id, partition_id)
            owning_nodes[part] = 0
            caching_nodes[part] = [n for n in range(1, n_nodes)]

    owning_nodes_map = [PartitionMap(k, v) for k, v in owning_nodes.items()]
    caching_nodes_map = [PartitionMap2(k, v) for k, v in caching_nodes.items()]
    return Partitioning(owning_nodes_map, caching_nodes_map, n_nodes)


def write_dataclass(problem, out_file: Path):
    data = asdict_with_json_names(problem)

    with out_file.open("w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)


T = TypeVar("T")


def load_dataclass(cls: type[T], data: dict) -> T:
    fields = {f.name for f in dataclasses.fields(cls)}
    return cls(**{k: v for k, v in data.items() if k in fields})


def get_cluster(core_count: int, network_speed: float, node_count: int = 3) -> Cluster:
    memory_size = 6
    network_latency = 1e-4  # 100 us
    nodes = [Node(core_count, memory_size, network_speed, network_latency) for _ in range(node_count)]

    return Cluster(nodes, False)


@dataclass
class LocatedTask:
    task: Task
    node: int


@dataclass
class Transfer:
    partition: Partition
    from_: int = field(metadata={"json_name": "from"})
    to: int


@dataclass
class ShuffleTarget:
    partition: Partition
    location: int  # node id


@dataclass
class TaskAssignment:
    locatedTasks: list[LocatedTask]
    transfers: list[Transfer]
    shuffleTargets: list[ShuffleTarget]


@dataclass
class SimulatorSettings:
    nParallelTasks: int
    allowStreamingInput: bool
    minimumBlockSize: float
    limitStepSize: bool
    stepLimit: float
    taskSlowdownFactors: Optional[list[list[float]]]

    @staticmethod
    def get_default() -> "SimulatorSettings":
        return SimulatorSettings(
            nParallelTasks=1_000_000,
            allowStreamingInput=True,
            minimumBlockSize=1024 * 1024,
            limitStepSize=True,
            stepLimit=1e-2,
            taskSlowdownFactors=None,
        )


def get_located_tasks(problem: AssignmentProblem, pipeline_plan: dict[int, Any]) -> list[LocatedTask]:
    result = []
    for taskmap in problem.tasks:
        pipeline_id = taskmap.k
        tasks = taskmap.v
        assignment = pipeline_plan[pipeline_id].task_assignment
        for i, t in enumerate(tasks):
            result.append(LocatedTask(t, assignment[i]))

    for tm in problem.broadcastTasks:
        pipeline_id, task = tm.k, tm.v
        assignment = pipeline_plan[pipeline_id].task_assignment
        assert len(assignment) == 1
        result.append(LocatedTask(task, assignment[0]))

    return result


def get_shuffle_targets(problem: AssignmentProblem, task_assignment: list[LocatedTask]) -> list[ShuffleTarget]:
    result = []
    # index tasks by scan / input DU
    du_tasks: dict[int, dict[int, int]] = {}
    for t in task_assignment:
        for p in [t.task.scannedPartition] + [p for p in t.task.requiredPartitions]:
            if p.dataUnitId not in du_tasks:
                du_tasks[p.dataUnitId] = {}
            du_tasks[p.dataUnitId][p.partitionIndex] = t.node

    for shuffle in problem.plan.shuffleStages:
        du = find_du(shuffle.to, problem.plan.dataUnits)
        mapping = None
        if du.id in du_tasks:
            mapping = du_tasks[du.id]

        if du.partitionLayout.type == "broadcast":
            assert du.partitionLayout.nPartitions == 1
            for i in range(len(problem.cluster.nodes)):
                result.append(ShuffleTarget(Partition(du.id, 0), i))
            continue

        if mapping is None:
            assert du.partitionLayout.nPartitions == 1
            result.append(ShuffleTarget(Partition(du.id, 0), 0))
            continue

        for i in range(du.partitionLayout.nPartitions):
            result.append(ShuffleTarget(Partition(du.id, i), mapping[i]))

    return result


def get_assignment(problem: AssignmentProblem, pipeline_plan: dict[int, Any]) -> TaskAssignment:
    located_tasks = get_located_tasks(problem, pipeline_plan)
    shuffle_targets = get_shuffle_targets(problem, located_tasks)
    transfers = []
    return TaskAssignment(located_tasks, transfers, shuffle_targets)
