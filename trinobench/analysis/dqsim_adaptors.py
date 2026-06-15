import json
import os
import subprocess
import tempfile
from pathlib import Path

from visualization.util.dqsim import (
    AssignmentProblem,
    SimulatorSettings,
    TaskAssignment,
    load_dataclass,
    write_dataclass,
)
from visualization.infra import DQSIM_DIR


def run_task_builder(problem: AssignmentProblem) -> AssignmentProblem:
    task_builder = Path(f"{DQSIM_DIR}/ds_task_builder")
    assert task_builder.exists(), f"Executable {task_builder} does not exist"

    with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as tmp:
        path = Path(tmp.name)
    write_dataclass(problem, path)
    # print(f"{task_builder} {path}")
    subprocess.run([task_builder, path], cwd=os.getcwd(), stdout=subprocess.PIPE, check=True)
    # print(result.stdout.decode())
    with open(path, "r") as f:
        data = json.load(f)
        # print(json.dumps(data))
    path.unlink(missing_ok=True)
    return load_dataclass(AssignmentProblem, data)


def create_assignment(problem: AssignmentProblem) -> TaskAssignment:
    task_planner = Path(f"{DQSIM_DIR}/ds_task_planner")
    assert task_planner.exists(), f"Executable {task_planner} does not exist"
    with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as tmp:
        path = Path(tmp.name)
    with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as tmp2:
        out_path = Path(tmp2.name)
    write_dataclass(problem, path)
    # print(f"{task_planner} {path} {out_path}")
    subprocess.run([task_planner, path, out_path], cwd=os.getcwd(), stdout=subprocess.PIPE, check=True)
    with open(out_path, "r") as f:
        data = json.load(f)
        # print(json.dumps(data))
    path.unlink(missing_ok=True)
    out_path.unlink(missing_ok=True)
    return load_dataclass(TaskAssignment, data)


def run_simulator(problem: AssignmentProblem, assignment: TaskAssignment, settings: SimulatorSettings) -> float:
    simulator = Path(f"{DQSIM_DIR}/ds_sim")
    assert simulator.exists(), f"Executable {simulator} does not exist"
    with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as tmp:
        problem_path = Path(tmp.name)
    with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as tmp2:
        assignment_path = Path(tmp2.name)
    with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as tmp3:
        settings_path = Path(tmp3.name)
    write_dataclass(problem, problem_path)
    write_dataclass(assignment, assignment_path)
    write_dataclass(settings, settings_path)
    # print(f"{simulator} -a {assignment_path} {problem_path} -s {settings_path}")
    # print(f"{simulator} -p -a {assignment_path} {problem_path} -s {settings_path} -o recording.json")
    result = subprocess.run(
        [simulator, "-p", "-a", assignment_path, problem_path, "-s", settings_path],
        cwd=os.getcwd(),
        stdout=subprocess.PIPE,
        check=True,
    )
    problem_path.unlink(missing_ok=True)
    assignment_path.unlink(missing_ok=True)
    settings_path.unlink(missing_ok=True)
    # print(result.stdout.decode())
    return float(result.stdout.decode().strip().split("\n")[-1].strip())
