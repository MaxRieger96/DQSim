To setup docker run:

```bash
sudo apt install docker.io
sudo systemctl enable --now docker
sudo usermod -aG docker $USER
newgrp docker
```

If the above fails because there is no docker group run

```bash
# check that the group exists
getent group docker
sudo groupadd docker
# show the groups
groups
```

Use the following `.env` configuration for data loading:

```
NUM_WORKERS=0
WORKER_CPUS=10
WORKER_MEMORY=30g
WORKER_MEMORY_GB=30
TRINO_HEAP_PCT=80
TRINO_QUERY_MAX_MEMORY=1TB
```

Start the cluster:

```bash
cd trinobench
docker compose up -d
```

Create the data

```bash
cd dqsim
uv run python trinobench/tpch/create_data.py
```

Run the Trino command line

```bash
trino --server http://localhost:8080
```

Run a sql file:

```bash
trino --server http://localhost:8080 < trinobench/tpch/tpch_load_sf1.sql
trino --server http://localhost:8080 < trinobench/tpch/tpch_load_sf10.sql
trino --server http://localhost:8080 < trinobench/tpch/tpch_load_sf100.sql
```

Run the following for testing:

```sql
DROP TABLE IF EXISTS local.tpch.nation;
DROP SCHEMA IF EXISTS local.tpch;

CREATE SCHEMA local.tpch;

CREATE TABLE local.tpch.nation
WITH (
  format = 'ORC'
)
AS SELECT * FROM tpch.sf1.nation;

SELECT * FROM local.tpch.nation;
```
