import json
from pathlib import Path
from typing import Optional

from pydantic.dataclasses import dataclass


def parse_time_string(s: str) -> float:
    # input needs to be float suffixed by time
    suffix_factors = {
        "ns": 1e-9,
        "us": 1e-6,
        "ms": 1e-3,
        "s": 1,
        "m": 60,
        "h": 3600,
        "d": 3600 * 24,
    }
    suffix = next((k for k in sorted(suffix_factors, key=len, reverse=True) if s.endswith(k)), None)
    assert suffix, f"String must end with one of {list(suffix_factors)}"
    num = s[: -len(suffix)]
    assert num, "No numeric value found before suffix"
    value = float(num)
    return value * suffix_factors[suffix]


def parse_byte_string(s: str) -> float:
    # input needs to be float suffixed by time
    suffix_factors = {
        "B": 1,
        "kB": 2**10,
        "MB": 2**20,
        "GB": 2**30,
        "TB": 2**40,
    }
    suffix = next((k for k in sorted(suffix_factors, key=len, reverse=True) if s.endswith(k)), None)
    assert suffix, f"String must end with one of {list(suffix_factors)}"
    num = s[: -len(suffix)]
    assert num, "No numeric value found before suffix"
    value = float(num)
    return value * suffix_factors[suffix]


def load_json_file(path: Path):
    with open(path, "r") as f:
        return json.load(f)


@dataclass
class PlanNode:
    id: int
    input_ids: list[int]
    parent_id: Optional[int]
    json: dict

    def __repr__(self):
        return (
            f"PlanNode(id={self.id}, "
            + f"input_ids={self.input_ids}, "
            + f"parent_id={self.parent_id}), "
            + f"type({self.json['@type']})"
        )


def find_source_op_json(op: dict) -> dict:
    # recursively goes to the source operator
    current = op
    while "source" in current:
        current = current["source"]
    if "left" in current:
        assert False, f"Called recursive source on op that is above join: {json.dumps(op)}"
    return current


@dataclass
class Plan:
    ops: dict[int, PlanNode]
    root_id: int

    def get(self, op_id: int) -> PlanNode:
        if op_id not in self.ops and op_id - 1 in self.ops:
            return self.ops[op_id - 1]
        assert op_id in self.ops
        return self.ops[op_id]

    def direct_parent(self, child: PlanNode) -> PlanNode:
        assert child.parent_id is not None
        return self.ops[child.parent_id]

    def is_direct_parent(self, parent: PlanNode, child: PlanNode) -> bool:
        assert (child.id in parent.input_ids) == (child.parent_id == parent.id)
        return child.parent_id == parent.id

    def is_transitive_parent(self, parent: PlanNode, child: PlanNode) -> bool:
        if self.is_direct_parent(parent, child):
            return True
        if child.id == self.root_id:
            return False
        return self.is_transitive_parent(parent, self.direct_parent(child))

    def to_dot(self) -> str:
        lines = ["digraph {"]
        for node in self.ops.values():
            label = f"id={node.id}\\ninputs={node.input_ids}\\nparent={node.parent_id}\\n{node.json['@type']}"
            lines.append(f'  {node.id} [label="{label}"]')
            for inp in node.input_ids:
                lines.append(f"  {node.id} -> {inp}")
        lines.append("}")
        return "\n".join(lines)


def load_plan_node(node: dict) -> tuple[PlanNode, list[dict]]:
    input_names = ["input", "source", "left", "right"]
    todos = []
    for n in input_names:
        if n in node:
            todos.append(node[n])
    if "sources" in node:
        for n in node["sources"]:
            todos.append(n)
    assert len(todos) > 0 or node["@type"] in ["tableScan", "remoteSource", "values"], (
        f"unexpected type: {node['@type']} in {node}"
    )
    return PlanNode(int(node["id"]), [], None, node), todos


def load_plan(plan: dict) -> Plan:
    nodes: dict[int, PlanNode] = {}
    todo: list[tuple[dict, Optional[int]]] = [(plan["root"], None)]
    root = None
    while len(todo) > 0:
        current, parent = todo[0]
        todo = todo[1:]
        current_node, added_todos = load_plan_node(current)
        if root is None:
            root = current_node.id
        if parent is not None:
            nodes[parent].input_ids.append(current_node.id)
            current_node.parent_id = parent
        nodes[current_node.id] = current_node
        todo += [(t, current_node.id) for t in added_todos]
    assert root is not None
    result = Plan(nodes, root)
    return result


@dataclass
class DataInfo:
    # information of the data that goes into or out of a pipeline
    cardinality: float
    n_bytes: float

    def __add__(self, other: "DataInfo") -> "DataInfo":
        if not isinstance(other, DataInfo):
            return NotImplemented
        return DataInfo(self.cardinality + other.cardinality, self.n_bytes + other.n_bytes)


@dataclass
class Pipeline:
    # There also is:
    #  - tasks.X.stats.pipelines.X.operatorSummaries.X.metrics.exchangeSerializerOutputBytes.total (operator specific)
    scans_stage_input: bool
    produces_task_output: bool
    # cpu time in seconds
    cpu_time: float
    # the maximum possible parallelism determined by the Trino drivers
    n_parallelism: int
    # the ops from the global op plan in the same order as the data flows
    # this can omit some ops
    # e.g. scan->filter->project pipelines will only include the last op
    # this means the scan and potentially also the filter might not be referenced here anymore
    op_ids: list[int]
    # the data that gets scanned from a storage service (compressed)
    base_rel_input: DataInfo
    # the data that gets scanned from cluster network (compressed)
    cluster_network_input: DataInfo
    # the data that gets processed (uncompressed)
    processed_input: DataInfo
    # the output data
    output_data: DataInfo
    # the time this pipeline was blocked on the output
    output_blocked_time: float

    def scans_base_table(self, plan: Plan) -> bool:
        op_json = plan.get(self.op_ids[0]).json
        op_json = find_source_op_json(op_json)
        type_field = "@type"
        if type_field not in op_json:
            assert False
            return False
        is_scan_type = op_json[type_field] in {
            "tableScan",
        }
        if not is_scan_type:
            assert op_json[type_field] in {"exchange", "remoteSource", "values"}, (
                f"unexpected source type {op_json[type_field]}"
            )
        return is_scan_type

    def scans_values(self, plan: Plan) -> bool:
        # if the tuples come from a values operator, this still counts as a base relation
        op_json = plan.get(self.op_ids[0]).json
        op_json = find_source_op_json(op_json)
        type_field = "@type"
        return op_json[type_field] == "values"

    def __add__(self, other: "Pipeline") -> "Pipeline":
        if not isinstance(other, Pipeline):
            return NotImplemented
        assert self.scans_stage_input == other.scans_stage_input
        assert self.produces_task_output == other.produces_task_output
        if self.op_ids != other.op_ids:
            if self.op_ids == []:
                res_op_ids = other.op_ids
            elif other.op_ids == []:
                res_op_ids = self.op_ids
            else:
                # we just take the longer list of ops
                if len(self.op_ids) > len(other.op_ids):
                    res_op_ids = self.op_ids
                else:
                    res_op_ids = other.op_ids
            assert res_op_ids == other.op_ids or res_op_ids == self.op_ids
        else:
            res_op_ids = self.op_ids
        # assert other.n_parallelism == 0 or self.n_parallelism == 0 or other.n_parallelism == self.n_parallelism
        res_parallelism = max(self.n_parallelism, other.n_parallelism)
        return Pipeline(
            self.scans_stage_input,
            self.produces_task_output,
            self.cpu_time + other.cpu_time,
            res_parallelism,
            res_op_ids,
            self.base_rel_input + other.base_rel_input,
            self.cluster_network_input + other.cluster_network_input,
            self.processed_input + other.processed_input,
            self.output_data + other.output_data,
            self.output_blocked_time + other.output_blocked_time,
        )


def load_pipeline(pipeline: dict) -> Pipeline:
    ops = [o["planNodeId"] for o in pipeline["operatorSummaries"]]
    return Pipeline(
        pipeline["inputPipeline"],
        pipeline["outputPipeline"],
        parse_time_string(pipeline["totalCpuTime"]),
        pipeline["totalDrivers"],
        ops,
        DataInfo(pipeline["physicalInputPositions"], parse_byte_string(pipeline["physicalInputDataSize"])),
        DataInfo(
            pipeline["internalNetworkInputPositions"], parse_byte_string(pipeline["internalNetworkInputDataSize"])
        ),
        DataInfo(pipeline["processedInputPositions"], parse_byte_string(pipeline["processedInputDataSize"])),
        DataInfo(pipeline["outputPositions"], parse_byte_string(pipeline["outputDataSize"])),
        parse_time_string(pipeline["outputBlockedTime"]),
    )


@dataclass
class Task:
    # Trino Task is a stage on a compute node
    # Tasks have:
    # - taskStatus.nodeid (str)
    # - outputBuffers.totalRowsSent (unnecessary?)
    node_id: str
    pipelines: list[Pipeline]


@dataclass
class CombinedTask:
    # A combined task represents the execution of a stage on all nodes.
    pipelines: list[Pipeline]
    # for each pipeline this holds a dict of participating nodes
    node_fractions: list[dict[str, float]]


def load_task(task: dict) -> Task:
    pipelines = [load_pipeline(p) for p in task["stats"]["pipelines"]]
    return Task(task["taskStatus"]["nodeId"], pipelines)


def combine_tasks(tasks: list[Task]) -> CombinedTask:
    result = CombinedTask(tasks[0].pipelines, [{tasks[0].node_id: p.cpu_time} for p in tasks[0].pipelines])
    for t in tasks[1:]:
        for i_pipeline, p in enumerate(t.pipelines):
            result.pipelines[i_pipeline] = result.pipelines[i_pipeline] + p
            result.node_fractions[i_pipeline][t.node_id] = p.cpu_time
    pipeline_sums = [sum(p.values()) for p in result.node_fractions]
    result.node_fractions = [
        {n: f / pipeline_sums[i] for n, f in p.items()} for i, p in enumerate(result.node_fractions)
    ]
    return result


@dataclass
class Table:
    index: int
    table_name: str


def load_table(table: dict, entry: str) -> Table:
    return Table(int(entry), table[entry]["tableName"])


@dataclass
class Stage:
    # a stage id
    id: int
    # a string id
    uid: str
    # number of compute nodes used (given by task length)
    n_used_nodes: int
    # cpu time in seconds
    cpu_time: float
    # the ids of stages that need to run before this stage
    input_stages: list[int]
    # the tables
    tables: list[Table]
    # the tasks this stage is executed in. Should be one per compute node
    tasks: list[Task]
    # the operator plan
    plan: Plan

    def check_integrity(self):
        assert len(self.tasks) == self.n_used_nodes
        for task in self.tasks:
            for pipeline in task.pipelines:
                # We allow one op to be missing if there are others
                missing_ops = []
                for op in pipeline.op_ids:
                    if op not in self.plan.ops and op - 1 not in self.plan.ops:
                        # print(f"Stage {self.id}: {op} not in {{{', '.join(str(x) for x in self.plan.ops.keys())}}}")
                        missing_ops.append(op)
                assert len(missing_ops) <= 1
                pipeline.op_ids = [e for e in pipeline.op_ids if e not in missing_ops]
                assert len(pipeline.op_ids) > 0 or len(missing_ops) == 0


def load_stage(stage: dict, id: int, stage_mapping: dict[str, int]) -> Stage:
    inputs = [stage_mapping[s] for s in stage["subStages"]]
    tasks = [load_task(task) for task in stage["tasks"]]

    for t in tasks[1:]:
        assert len(t.pipelines) == len(tasks[0].pipelines)
        for i_pipeline, p1 in enumerate(tasks[0].pipelines):
            p2 = t.pipelines[i_pipeline]
            if len(p1.op_ids) == 0 or len(p2.op_ids) == 0:
                continue
            if len(p1.op_ids) != len(p2.op_ids) and set(p1.op_ids) != set(p2.op_ids):
                print(f"Tasks deviate! {p1.op_ids} != {p2.op_ids}\n{tasks[0]}\n{t}\n")
            # We rely on the op ids of each task to be similar enough. The below assertions are too strict,
            # e.g. dynamic filters can be placed in a subset of tasks.
            # The checks below are to strict
            # assert len(p1.op_ids) == len(p2.op_ids) or len(p1.op_ids) == 0 or len(p2.op_ids) == 0
            # for i_op, op1 in enumerate(p1.op_ids):
            #     op2 = p2.op_ids[i_op]
            #     assert op1 == op2

    tables = [load_table(stage["tables"], i) for i in stage["tables"]]
    return Stage(
        id,
        stage["stageId"],
        len(stage["tasks"]),
        parse_time_string(stage["stageStats"]["totalCpuTime"]),
        inputs,
        tables,
        tasks,
        load_plan(stage["plan"]),
    )


@dataclass
class Query:
    execution_time: float
    stages: list[Stage]


def load_query(stats: dict) -> Query:
    stage_mapping = {s["stageId"]: i for i, s in enumerate(stats["stages"]["stages"])}
    stages = []
    for i, stage in enumerate(stats["stages"]["stages"]):
        curr = load_stage(stage, i, stage_mapping)
        curr.check_integrity()
        stages.append(curr)
    return Query(parse_time_string(stats["queryStats"]["executionTime"]), stages)
