import json
import shutil
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

import requests
import trino

TRINO_DIR = Path("trinobench")
TRINO_HOST = "localhost"
TRINO_PORT = 8080


def sh(cmd: str, **kw) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, shell=True, cwd=TRINO_DIR, text=True, **kw)


def _conn(catalog: str, schema: str) -> trino.dbapi.Connection:
    return trino.dbapi.connect(
        host=TRINO_HOST,
        port=TRINO_PORT,
        user="benchmarker",
        catalog=catalog,
        schema=schema,
    )


def _query(sql: str) -> list:
    with _conn("local", "tpch1") as conn:
        cur = conn.cursor()
        cur.execute(sql)
        return cur.fetchall()


def _fetch_query_json(query_id: str) -> dict:
    base = f"http://{TRINO_HOST}:{TRINO_PORT}"
    s = requests.Session()
    s.post(f"{base}/ui/login", data={"username": "benchmarker"})
    resp = s.get(f"{base}/ui/api/query/{query_id}")
    resp.raise_for_status()
    return resp.json()


@dataclass
class Cluster:
    workers: int = 2
    cpus: int = 4
    memory_gb: int = 10
    heap_pct: int = 80
    stats_dir: str = "results"  # directory for JSON stats

    def _write_env(self):
        (TRINO_DIR / ".env").write_text(
            f"NUM_WORKERS={self.workers}\n"
            f"WORKER_CPUS={self.cpus}\n"
            f"WORKER_MEMORY={self.memory_gb}g\n"
            f"WORKER_MEMORY_GB={self.memory_gb}\n"
            f"TRINO_HEAP_PCT={self.heap_pct}\n"
            f"TRINO_QUERY_MAX_MEMORY=1TB\n"
        )

    def up(self):
        self._write_env()
        print(f"=> Starting cluster: {self.workers}w × {self.cpus}cpu × {self.memory_gb}GB")
        sh("docker compose down --remove-orphans -t5")
        sh("docker compose up -d --force-recreate --wait")
        self._wait_for_workers()

    def down(self):
        sh("docker compose down --remove-orphans -t5")

    def run(self, sql_file: Path, sf: int, out_file: Path, phony: bool = False) -> dict:
        """Run a SQL file. Saves full query stats JSON and returns it."""
        print(f"Running query at {sql_file} and storing results to {out_file}")
        sql = sql_file.read_text().strip().rstrip(";")
        # query_name = Path(sql_file).stem
        print(f"=> Running {sql_file}...", end="", flush=True)
        t0 = time.time()

        conn = _conn(catalog="local", schema=f"tpch{sf}")
        cur = conn.cursor()
        cur.execute(sql)
        cur.fetchall()
        elapsed = time.time() - t0

        stats = _fetch_query_json(cur.query_id)
        conn.close()

        if not phony:
            out_file.parent.mkdir(exist_ok=True)
            out_file.write_text(json.dumps(stats, indent=2))

        print(f" {elapsed:.2f}s → {out_file}")
        return stats

    def _wait_for_workers(self, timeout=120):
        print(f"=> Waiting for {self.workers} workers...", end="", flush=True)
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                rows = _query("SELECT count(*) FROM system.runtime.nodes WHERE coordinator = false")
                if rows[0][0] >= self.workers:
                    print(" ready")
                    return
            except Exception:
                pass
            print(".", end="", flush=True)
            time.sleep(2)
        print(f" TIMEOUT (wanted {self.workers})")

    @staticmethod
    def configure_bandwidth(bandwidth: Optional[str]):
        script = TRINO_DIR / "configure_bandwidth.sh"
        if not script.exists():
            return
        if bandwidth is not None:
            print(f"=> Bandwidth: {bandwidth}")
            sh(f"bash configure_bandwidth.sh {bandwidth}")
        else:
            sh("bash configure_bandwidth.sh reset")


def setup_tc_nopasswd():
    tc_path = shutil.which("tc")
    if tc_path is None:
        raise RuntimeError("tc not found in PATH")
    rule = f"{subprocess.run(['whoami'], capture_output=True, text=True).stdout.strip()} ALL=(ALL) NOPASSWD: {tc_path}"
    subprocess.run(["sudo", "tee", "/etc/sudoers.d/tc-nopasswd"], input=rule + "\n", text=True, check=True)
    print(f"✓ Installed sudoers rule: {rule}")


def teardown_tc_nopasswd():
    subprocess.run(["sudo", "rm", "-f", "/etc/sudoers.d/tc-nopasswd"], check=True)
    print("✓ Removed sudoers rule")


def get_sql_files(directory: Path) -> list[Path]:
    return sorted(list(Path(directory).rglob("*.sql")))


def get_queries(workload: str) -> list[Path]:
    if workload == "test":
        return get_sql_files(Path("trinobench/tpch/queries/"))
    elif workload == "tpch":
        return get_sql_files(Path("queries/tpch/"))
    assert False, f"unknown workload: {workload}"


@dataclass
class RunConfig:
    n_runs: int
    workloads: list[str]
    scale_factors: list[int]
    worker_counts: list[int]  # 0 is only the coordinator, which is a worker too
    cpu_counts: list[int]
    bandwidths: list["str"]  # supports mbit and gbit suffix

    def run(self, phony: bool = False, avoid_restart: bool = False):
        for workers in self.worker_counts:
            for cpus in self.cpu_counts:
                c = Cluster(workers=workers, cpus=cpus, memory_gb=10)
                if not avoid_restart:
                    c.up()
                for bandwidth in self.bandwidths:
                    c.configure_bandwidth(bandwidth)
                    for scale_factor in self.scale_factors:
                        for workload in self.workloads:
                            for query in get_queries(workload):
                                for repeat in range(self.n_runs):
                                    out_name = f"{workload}_{query.stem}_{workers + 1}w_{cpus}c_{bandwidth}_sf{scale_factor}_r{repeat}"
                                    # print(f"workload: {workload}")
                                    # print(f"out_name: {out_name}")
                                    out_file = Path(f"trinobench/results/{workload}/{out_name}.json")
                                    if out_file.exists() and not phony:
                                        print(f" skipped {out_name}")
                                        continue
                                    try:
                                        c.run(query, scale_factor, out_file, phony)
                                    except Exception as e:
                                        print(f"FAILED {out_name}: {e}")
                                        # Optionally write a failure marker so you know it was attempted:
                                        out_file.parent.mkdir(exist_ok=True)
                                        out_file.with_suffix(".error").write_text(str(e))


def start_cluster_for_manual_interaction():
    setup_tc_nopasswd()
    RunConfig(1, ["tpch"], [10], [0], [1], ["10gbit"]).run(True, True)
    exit()


def main():
    # start_cluster_for_manual_interaction()
    n_runs = 5
    workloads = ["test", "tpch"]
    bandwidths = ["10gbit", "5gbit", "1gbit", "500mbit"]
    scale_factors = [1, 10, 100]
    def_bandwidth = "5gbit"
    def_cores = 4
    def_workers = 2

    # RunConfig(n_runs, ["test"], [10], [2], [2], ["10gbit"]).run(phony=True)
    # RunConfig(n_runs, ["test"], scale_factors, [3, 2, 1, 0], [3, 1], ["10gbit"]).run()
    # exit()

    # running the full grid like this takes weeks
    setup_tc_nopasswd()
    # scale factors
    RunConfig(n_runs, workloads, scale_factors, [def_workers], [def_cores], [def_bandwidth]).run()
    # node count
    RunConfig(n_runs, workloads, scale_factors, [6, 5, 4, 3, 2, 1, 0], [2], [def_bandwidth]).run()
    # core count
    RunConfig(n_runs, workloads, scale_factors, [2], [6, 5, 4, 3, 2, 1], [def_bandwidth]).run()
    # bandwidths
    RunConfig(n_runs, workloads, scale_factors, [def_workers], [def_cores], bandwidths).run()
    # simple stuff to understand what's going on
    # RunConfig(n_runs, ["test"], scale_factors, [2], [4, 1], bandwidths).run()
    # RunConfig(n_runs, ["test"], scale_factors, [2, 1, 0], [4, 1], ["10gbit"]).run()
    # RunConfig(n_runs, ["test"], scale_factors, [0], [4, 2, 1], bandwidths).run()
    # RunConfig(n_runs, ["test"], [10], [0], [12, 10, 8, 6, 4, 2, 1], ["10gbit"]).run()
    # simple stuff on tpch
    # RunConfig(n_runs, ["test"], [10], [0], [1], ["1gbit"]).run()
    teardown_tc_nopasswd()


if __name__ == "__main__":
    main()
