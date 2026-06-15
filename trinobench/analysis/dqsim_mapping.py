from copy import deepcopy
from typing import Optional

from pydantic.dataclasses import dataclass

from trinobench.analysis.trino_reading import CombinedTask, Pipeline, Plan, Query, Stage, Task, combine_tasks
from visualization.util.dqsim import DU, DistributedPlan, FullDU, FullPipeline, Shuffle, createFullDU


@dataclass
class PipelineDeps:
    pipeline: Pipeline
    local_pipeline_id: int
    scan_pipeline_id: Optional[int]
    required_pipeline_ids: list[int]
    stage_id: int
    slowdown_factors: list[float]

    def get_dependencies(self) -> list[int]:
        # all dependencies including the scanned pipeline
        scan_pipeline_list = []
        if self.scan_pipeline_id is not None:
            scan_pipeline_list = [self.scan_pipeline_id]
        return self.required_pipeline_ids + scan_pipeline_list

    def __repr__(self) -> str:
        return f"PipelineDeps(id={self.local_pipeline_id}, scan={self.scan_pipeline_id}) required={self.required_pipeline_ids} ops={self.pipeline.op_ids}"

    def increase_pipeline_id(self, n: int) -> "PipelineDeps":
        self.local_pipeline_id += n
        return self


@dataclass
class PipelineDUs:
    out_du: int
    scan_du: int
    required_dus: list[int]

    def increase_du_id(self, n: int) -> "PipelineDUs":
        self.out_du += n
        self.scan_du += n
        self.required_dus = [du + n for du in self.required_dus]
        return self

    def remap_dus(self, map: dict[int, int]) -> "PipelineDUs":
        self.out_du = map.get(self.out_du, self.out_du)
        self.scan_du = map.get(self.scan_du, self.scan_du)
        self.required_dus = [map.get(du, du) for du in self.required_dus]
        return self


@dataclass
class PipelineGraph:
    pipelines: list[PipelineDeps]
    pipeline_dus: dict[int, PipelineDUs]
    dus: dict[int, DU]
    shuffles: list[tuple[int, int]]

    def get(self, pipeline_id: int) -> PipelineDeps:
        assert self.pipelines[pipeline_id].local_pipeline_id == pipeline_id
        return self.pipelines[pipeline_id]

    def depends_directly(self, successor: PipelineDeps, predecessor: PipelineDeps) -> bool:
        return predecessor.local_pipeline_id in successor.get_dependencies()

    def depends_transitively(self, successor: PipelineDeps, predecessor: PipelineDeps) -> bool:
        if self.depends_directly(successor, predecessor):
            return True
        for dep in successor.get_dependencies():
            if self.depends_transitively(self.get(dep), predecessor):
                return True
        return False

    def to_dot(self) -> str:
        lines = ["digraph {"]
        for p in self.pipelines:
            lines.append(
                f'  {p.local_pipeline_id} [label="id={p.local_pipeline_id}\\nscan={p.scan_pipeline_id}\\nrequired={p.required_pipeline_ids}\\nops={", ".join(str(x) for x in p.pipeline.op_ids)}"]'
            )
            for dep in p.get_dependencies():
                lines.append(f"  {p.local_pipeline_id} -> {dep}")
        lines.append("}")
        return "\n".join(lines)

    def to_dot_with_dus(self) -> str:
        lines = ["digraph {"]
        # Pipeline nodes
        for p in self.pipelines:
            lines.append(
                f'  p{p.local_pipeline_id} [shape=box label="pipeline {p.local_pipeline_id}\\nops:({", ".join(str(p) for p in p.pipeline.op_ids)})"]'
            )
        # DU nodes
        for du_id in self.dus:
            du = self.dus[du_id]
            lines.append(f'  du{du_id} [shape=ellipse label="DU {du_id}({du.id})\\n base_rel: {du.is_base_relation}"]')
        # Edges
        for pid, pdu in self.pipeline_dus.items():
            lines.append(f"  du{pdu.scan_du} -> p{pid} [label=scan]")
            for du_id in pdu.required_dus:
                lines.append(f"  du{du_id} -> p{pid} [label=req]")
            lines.append(f"  p{pid} -> du{pdu.out_du} [label=out]")
        # Shuffles
        for from_du, to_du in self.shuffles:
            lines.append(f"  du{from_du} -> du{to_du}[label=shuffle]")
        lines.append("}")
        return "\n".join(lines)

    def create_dus(self, plan: Plan):
        next_id = 0

        # first output dus
        out_dus: dict[int, int] = {}
        for p in self.pipelines:
            current_id = next_id
            next_id += 1
            byte_size = 0
            if p.pipeline.output_data.cardinality > 0:
                byte_size = p.pipeline.output_data.n_bytes / p.pipeline.output_data.cardinality
            # Partitioning info is set correctly later when converting the PipelineGraph to a DistributedPlan
            out_du = DU(
                id=current_id,
                cardinality=p.pipeline.output_data.cardinality,
                byte_size=byte_size,
                broadcast=False,
                single_node=True,
                is_base_relation=False,
            )
            self.dus[out_du.id] = out_du
            out_dus[p.local_pipeline_id] = out_du.id

        # next scan dus that do not come from other pipelines
        base_scan_dus: dict[int, int] = {}
        for p in self.pipelines:
            if p.scan_pipeline_id is None:
                current_id = next_id
                next_id += 1
                is_base_rel = p.pipeline.scans_base_table(plan)
                is_values = p.pipeline.scans_values(plan)
                # base_rel_input is the physical input, so it will reflect compression.
                # I do not see a reason, why we should ever need to use processed_input
                input_data_info = p.pipeline.base_rel_input
                if input_data_info.cardinality == 0:
                    assert not is_base_rel
                    input_data_info = p.pipeline.processed_input
                if input_data_info.cardinality == 0:
                    # print(f"Warning, found empty intermediate result. This is normal for tpch q11")
                    byte_size = 0
                else:
                    byte_size = input_data_info.n_bytes / input_data_info.cardinality
                scan_du = DU(
                    id=current_id,
                    cardinality=input_data_info.cardinality,
                    byte_size=byte_size,
                    broadcast=False,
                    single_node=True,
                    is_base_relation=is_base_rel or is_values,
                )
                self.dus[scan_du.id] = scan_du
                base_scan_dus[p.local_pipeline_id] = scan_du.id

        # now put all of them in the right place
        for p in self.pipelines:
            scan_du = None
            if p.local_pipeline_id in base_scan_dus:
                scan_du = base_scan_dus[p.local_pipeline_id]
            else:
                assert p.scan_pipeline_id is not None
                scan_du = out_dus[p.scan_pipeline_id]
            required_dus = [out_dus[r] for r in p.required_pipeline_ids]
            current_pipeline_dus = PipelineDUs(
                out_du=p.local_pipeline_id,
                scan_du=scan_du,
                required_dus=required_dus,
            )
            self.pipeline_dus[p.local_pipeline_id] = current_pipeline_dus

    def increase_pipeline_ids(self, n: int):
        new_pipelines = [p.increase_pipeline_id(n) for p in self.pipelines]
        self.pipelines = new_pipelines
        new_pipeline_dus: dict[int, PipelineDUs] = {p + n: pdu for p, pdu in self.pipeline_dus.items()}
        self.pipeline_dus = new_pipeline_dus

    def increase_du_ids(self, n: int):
        new_pipeline_dus = {p: pdu.increase_du_id(n) for p, pdu in self.pipeline_dus.items()}
        self.pipeline_dus = new_pipeline_dus
        self.dus = {duid + n: du.increase_du_id(n) for duid, du in self.dus.items()}

    def remap_du_references(self, map: dict[int, int]):
        new_pipeline_dus = {p: pdu.remap_dus(map) for p, pdu in self.pipeline_dus.items()}
        self.pipeline_dus = new_pipeline_dus
        self.shuffles = [(map.get(from_, from_), map.get(to, to)) for from_, to in self.shuffles]
        # self.dus = {map.get(duid, duid): du.remap_dus(map) for duid, du in self.dus.items()}

    def merge_with_other(self, other: "PipelineGraph"):
        common_dus = set(self.dus).intersection(set(other.dus))
        assert len(common_dus) == 0, f"{common_dus} are present in {set(self.dus)} and {set(other.dus)}"
        common_pipeline_ids = {p.local_pipeline_id for p in self.pipelines}.intersection(
            {p.local_pipeline_id for p in other.pipelines}
        )
        assert len(common_pipeline_ids) == 0
        self.pipelines = self.pipelines + other.pipelines
        self.dus = self.dus | other.dus
        self.pipeline_dus = self.pipeline_dus | other.pipeline_dus

    def find_output_du(self) -> int:
        assert self.pipelines[-1].pipeline.produces_task_output
        return self.pipeline_dus[self.pipelines[-1].local_pipeline_id].out_du

    def find_stage_input_dus(self, plan: Plan) -> list[int]:
        res: list[PipelineDeps] = []
        for p in self.pipelines:
            if p.scan_pipeline_id is None:
                type = plan.get(p.pipeline.op_ids[0]).json["@type"]
                if type in ["remoteSource", "exchange"]:
                    res.append(p)
        assert len(res) > 0
        return [self.pipeline_dus[r.local_pipeline_id].scan_du for r in res]

    def make_ids_continuous(self):
        # make sure that there are no gaps in the du ids
        # first collect all used du ids in the used order
        used_dus = []
        # here we assume that pipeline ids are in dependency order
        for pid in range(len(self.pipelines)):
            p = self.pipeline_dus[pid]
            if p.scan_du not in used_dus:
                used_dus.append(p.scan_du)
            assert p.out_du not in used_dus
            used_dus.append(p.out_du)

        du_map = {du: i for i, du in enumerate(used_dus)}
        self.remap_du_references(du_map)
        self.dus = {du_map.get(duid, duid): du.remap_dus(du_map) for duid, du in self.dus.items()}
        # remove unused dus
        valid_du_ids = {i for i in du_map.values()}
        self.dus = {k: v for k, v in self.dus.items() if k in valid_du_ids}
        for du_id, du in self.dus.items():
            assert du_id == du.id

    def _get_new_du_id(self) -> int:
        max_du_id = -1
        for p in self.pipeline_dus.values():
            max_du_id = max(max_du_id, p.scan_du, p.out_du)
        assert max_du_id >= 0 or len(self.pipeline_dus) == 0
        return max_du_id + 1

    def _insert_shuffle(self, du_id: int, creating_pipeline: PipelineDUs):
        new_du_id = self._get_new_du_id()
        self.shuffles.append((new_du_id, du_id))
        creating_pipeline.out_du = new_du_id
        new_du = deepcopy(self.dus[du_id])
        new_du.id = new_du_id
        self.dus[new_du_id] = new_du

    def create_shuffles(self, stages: list[Stage]):
        pipeline_output_lookup = {self.pipeline_dus[p.local_pipeline_id].out_du: p for p in self.pipelines}
        already_shuffled = set()
        for p in self.pipelines:
            # stage = stage_lookup[p.stage_id]
            du_info = self.pipeline_dus[p.local_pipeline_id]
            for input_du in [du_info.scan_du] + du_info.required_dus:
                if input_du not in pipeline_output_lookup:
                    continue
                if input_du in already_shuffled:
                    continue
                input_pipeline = pipeline_output_lookup[input_du]
                if p.stage_id != input_pipeline.stage_id:
                    self._insert_shuffle(input_du, self.pipeline_dus[input_pipeline.local_pipeline_id])
                    already_shuffled.add(input_du)

    def get_pipelines(self) -> list[FullPipeline]:
        pipelines: list[FullPipeline] = []
        for p in self.pipelines:
            dus = self.pipeline_dus[p.local_pipeline_id]
            in_card = self.dus[dus.scan_du].cardinality
            if in_card == 0:
                load = 0
            else:
                load = p.pipeline.cpu_time / in_card / 16
            pipelines.append(
                FullPipeline(
                    p.local_pipeline_id,
                    dus.scan_du,
                    dus.out_du,
                    load,
                    dus.required_dus,
                    f"pipeline_{p.local_pipeline_id}",
                )
            )
        return pipelines

    def to_distributed_plan(self: "PipelineGraph", stages: list[Stage]) -> tuple[DistributedPlan, list[list[float]]]:
        # create a lookup structure to find involved pipelines for each du
        du_pipelines: dict[int, PipelineDeps] = {}
        slowdown_factors = []
        for p in self.pipelines:
            du_info = self.pipeline_dus[p.local_pipeline_id]
            du_pipelines[du_info.out_du] = p
            slowdown_factors.append(p.slowdown_factors)
            for in_du in [du_info.scan_du] + du_info.required_dus:
                if in_du not in du_pipelines:
                    du_pipelines[in_du] = p
        # find stages by id
        stage_lookup = {s.id: s for s in stages}
        dus: list[FullDU] = []
        for du in self.dus.values():
            pipeline = du_pipelines[du.id]
            # read parallelism from stage.
            # everything was set to single node before, now we use hash-partitioned for all things that have a larger
            # parallelism than 1
            parallelism = stage_lookup[pipeline.stage_id].n_used_nodes
            if parallelism != 1:
                du.single_node = False
                du.broadcast = False
            dus.append(createFullDU(du, parallelism))
        dus.sort(key=lambda x: x.id)
        for i, du in enumerate(dus):
            assert i == du.id
        pipelines: list[FullPipeline] = self.get_pipelines()
        shuffles: list[Shuffle] = [Shuffle(from_, to) for from_, to in self.shuffles]
        dep_order: list[int] = [p.local_pipeline_id for p in self.pipelines]

        return DistributedPlan(dus, pipelines, shuffles, dep_order), slowdown_factors


def filter_ops_with_dependencies(pipeline: Pipeline, plan: Plan, already_filtered: set[int]) -> list[int]:
    if (
        (len(pipeline.op_ids) >= 2)
        and (pipeline.op_ids[-1] not in already_filtered)
        and (plan.get(pipeline.op_ids[-1]).json["@type"] == "join")
    ):
        already_filtered.add(pipeline.op_ids[-1])
        if pipeline.op_ids[-1] == pipeline.op_ids[-2]:
            return pipeline.op_ids[:-2]
    return pipeline.op_ids


def load_pipelines(t: CombinedTask, plan: Plan, stage_id: int) -> PipelineGraph:
    # print(plan.to_dot())
    next_id = 0
    # maps from last op id to the pipeline
    pipelines: PipelineGraph = PipelineGraph([], {}, {}, [])
    already_filtered: set[int] = set()
    for i_pipeline, p in enumerate(t.pipelines):
        current_id = next_id
        next_id += 1
        dependencies: list[int] = []
        for op in filter_ops_with_dependencies(p, plan, already_filtered):
            # for op in p.op_ids:
            for p2 in pipelines.pipelines[::-1]:
                # using this special order, the scanned pipeline will always be the first dependency

                if p2.local_pipeline_id in dependencies:
                    continue

                p2_last_op = plan.get(p2.pipeline.op_ids[-1])
                # print(f"p: {current_id} op: {op}, p2: {p2.local_pipeline_id}, p2_op: {p2_last_op.id}")
                if op == p2_last_op.id or plan.is_transitive_parent(plan.get(op), p2_last_op):
                    # shadow dependent pipelines if there is a intermediate pipeline
                    already_depends_transitively = False
                    for dep in dependencies:
                        dep_pipeline = pipelines.get(dep)
                        if pipelines.depends_transitively(dep_pipeline, p2):
                            already_depends_transitively = True
                            break
                    if not already_depends_transitively:
                        dependencies.append(p2.local_pipeline_id)
                        # print(f"added {p2.local_pipeline_id}")

        # filter redundant dependencies
        minimal = dependencies[:1]
        for dep_id in dependencies[1:]:
            dep = pipelines.get(dep_id)
            if not any(
                pipelines.depends_transitively(pipelines.get(other), dep) for other in dependencies if other != dep_id
            ):
                minimal.append(dep_id)
        dependencies = minimal

        scanned_pipeline = None
        remaining_dependencies = dependencies
        if len(dependencies) > 0:
            first_dependency = dependencies[0]
            dependency_last_op = plan.get(pipelines.pipelines[first_dependency].pipeline.op_ids[-1])
            current_first_op = plan.get(p.op_ids[0])
            if (current_first_op.id == dependency_last_op.id) or plan.is_transitive_parent(
                current_first_op, dependency_last_op
            ):
                assert (current_first_op.id == dependency_last_op.id) or plan.is_direct_parent(
                    current_first_op, dependency_last_op
                ), (
                    f"we assume that {current_first_op.id} comes directly after {dependency_last_op.id}, because we inferred that pipeline {current_id} scans the output of pipeline {first_dependency}"
                )
                scanned_pipeline = first_dependency
                remaining_dependencies = dependencies[1:]
        assert abs(sum(t.node_fractions[i_pipeline].values()) - 1.0) < 1e-4
        slowdown_factors = [
            cpu_time * len(t.node_fractions[i_pipeline]) for cpu_time in t.node_fractions[i_pipeline].values()
        ]
        current_pipeline = PipelineDeps(
            p, current_id, scanned_pipeline, remaining_dependencies, stage_id, slowdown_factors
        )
        # print(current_pipeline)
        pipelines.pipelines.append(current_pipeline)

    assert sorted(pipelines.pipelines, key=lambda x: x.local_pipeline_id) == pipelines.pipelines
    return pipelines


def match_dus(out_dus: list[int], in_dus: list[int], graph: PipelineGraph):
    assert len(out_dus) == len(in_dus)
    # the out dus should stay in the same order but the in du_s may change and should correspond to the correct du
    out_du_sizes = {(graph.dus[du].cardinality,): i for i, du in enumerate(out_dus)}
    assert len(out_du_sizes) == len(out_dus)

    out_to_in_indexes: list[int] = [-1 for _ in out_dus]
    matched_in_du_indexes = set()
    for i, in_du in enumerate(in_dus):
        size = (graph.dus[in_du].cardinality,)
        if size in out_du_sizes:
            out_to_in_indexes[out_du_sizes[size]] = i
            matched_in_du_indexes.add(i)
        else:
            pass
            # print(f"Could not find {size} in {out_du_sizes}")
    unmatched_in_du_indexes = list(set(range(len(in_dus))) - matched_in_du_indexes)

    for i in out_to_in_indexes:
        if out_to_in_indexes[i] == -1:
            out_to_in_indexes[i] = unmatched_in_du_indexes[0]
            unmatched_in_du_indexes = unmatched_in_du_indexes[1:]

    return [in_dus[i] for i in out_to_in_indexes]


def connect_stage_pipeline_graphs(stage_graphs: dict[int, PipelineGraph], query: Query) -> PipelineGraph:
    # This info is already present in form of input stages
    # maps from stage id to a combined plan
    combined_graphs: dict[int, PipelineGraph] = {
        stage.id: deepcopy(stage_graphs[stage.id]) for stage in query.stages if len(stage.input_stages) == 0
    }
    # stages are sorted in reverse dependency order
    for s in query.stages[::-1]:
        stage_id = s.id
        if len(s.input_stages) == 0:
            continue

        current_input_graphs: list[PipelineGraph] = []
        for in_stage in s.input_stages:
            current_input_graphs.append(combined_graphs[in_stage])

        merged_input_graphs: PipelineGraph = deepcopy(current_input_graphs[0])
        for other in current_input_graphs[1:]:
            other.increase_du_ids(len(merged_input_graphs.dus))
            other.increase_pipeline_ids(len(merged_input_graphs.pipelines))
            merged_input_graphs.merge_with_other(other)

        stage_graph = stage_graphs[stage_id]
        stage_graph.increase_du_ids(len(merged_input_graphs.dus))
        stage_graph.increase_pipeline_ids(len(merged_input_graphs.pipelines))
        merged_input_graphs.merge_with_other(stage_graph)

        # find common DU and make id the same
        out_dus = [graph.find_output_du() for graph in current_input_graphs]
        in_dus = stage_graph.find_stage_input_dus(s.plan)
        in_dus = match_dus(out_dus, in_dus, merged_input_graphs)
        merged_input_graphs.remap_du_references({in_du: out_du for in_du, out_du in zip(in_dus, out_dus)})

        combined_graphs[stage_id] = merged_input_graphs

    return combined_graphs[0]  # 0 is the root stage


def convert_trino_plan(query: Query) -> tuple[DistributedPlan, list[list[float]], str]:
    pipeline_graphs: dict[int, PipelineGraph] = {}
    for s in query.stages:
        # print(f"stage: {s.id}")
        combined_task = combine_tasks(s.tasks)
        current_pipelines: PipelineGraph = load_pipelines(combined_task, s.plan, s.id)
        # print(current_pipelines.to_dot())
        current_pipelines.create_dus(s.plan)
        pipeline_graphs[s.id] = current_pipelines

    # stitch together stages to a single plan
    whole_plan: PipelineGraph = connect_stage_pipeline_graphs(pipeline_graphs, query)
    # print(whole_plan.to_dot_with_dus())
    whole_plan.create_shuffles(query.stages)
    whole_plan.make_ids_continuous()
    # print(whole_plan.to_dot_with_dus())
    distributed_plan, slowdown_factors = whole_plan.to_distributed_plan(query.stages)
    distributed_plan.augment_final_shuffle_if_necessary()
    # print(whole_plan.to_dot_with_dus())
    return distributed_plan, slowdown_factors, whole_plan.to_dot_with_dus()
