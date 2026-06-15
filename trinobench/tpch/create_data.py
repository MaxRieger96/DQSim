import duckdb

MINIO_ENDPOINT = "localhost:9000"  # or localhost:9000 if running outside Docker
MINIO_ACCESS_KEY = "minioadmin"
MINIO_SECRET_KEY = "minioadmin"
MINIO_BUCKET = "trino"

TPCH_TABLES = [
    "lineitem",
    "orders",
    "customer",
    "partsupp",
    "part",
    "supplier",
    "nation",
    "region",
]


def tpch_tables_already_loaded(con) -> bool:
    existing_tables = {row[0] for row in con.execute("SHOW TABLES").fetchall()}
    if not all(t in existing_tables for t in TPCH_TABLES):
        return False
    # Also verify they're non-empty (in case a previous run was interrupted)
    for table in TPCH_TABLES:
        count = con.execute(f"SELECT COUNT(*) FROM {table}").fetchone()[0]
        if count == 0:
            return False
    return True


def export_tpch_to_minio(scale_factor: int | float):
    sf = int(scale_factor) if float(scale_factor).is_integer() else scale_factor
    prefix = f"s3://{MINIO_BUCKET}/staging/sf{sf}"

    print(f"Generating TPC-H SF{sf} and uploading to {prefix}/")

    con = duckdb.connect(f"tpch{scale_factor}.duckdb")
    con.execute("INSTALL httpfs; LOAD httpfs;")
    con.execute("INSTALL tpch;  LOAD tpch;")
    con.execute("SET memory_limit='10GB';")
    con.execute("SET temp_directory='/tmp/duckdb';")

    # Point DuckDB at MinIO
    con.execute(f"""
        SET s3_endpoint    = '{MINIO_ENDPOINT.removeprefix("http://").removeprefix("https://")}';
        SET s3_access_key_id     = '{MINIO_ACCESS_KEY}';
        SET s3_secret_access_key = '{MINIO_SECRET_KEY}';
        SET s3_use_ssl           = false;
        SET s3_url_style         = 'path';
    """)

    if tpch_tables_already_loaded(con):
        print("TPC-H tables already present in DuckDB file, skipping dbgen.")
    else:
        print("Running dbgen")
        con.execute(f"CALL dbgen(sf={scale_factor});")

    for table in TPCH_TABLES:
        print(f"Writing {table}")
        # One subdirectory per table so Trino external_location is unambiguous
        out = f"{prefix}/{table}/{table}.parquet"
        con.execute(f"COPY {table} TO '{out}' (FORMAT PARQUET, COMPRESSION ZSTD);")
        count = con.execute(f"SELECT COUNT(*) FROM {table}").fetchone()[0]
        print(f"  {table}: {count:,} rows -> {out}")

    con.close()
    print()


if __name__ == "__main__":
    export_tpch_to_minio(scale_factor=1)
    export_tpch_to_minio(scale_factor=10)
    export_tpch_to_minio(scale_factor=100)
