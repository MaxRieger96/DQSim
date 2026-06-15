DROP TABLE IF EXISTS local.tpch1_staging.lineitem;
DROP TABLE IF EXISTS local.tpch1_staging.orders;
DROP TABLE IF EXISTS local.tpch1_staging.customer;
DROP TABLE IF EXISTS local.tpch1_staging.partsupp;
DROP TABLE IF EXISTS local.tpch1_staging.part;
DROP TABLE IF EXISTS local.tpch1_staging.supplier;
DROP TABLE IF EXISTS local.tpch1_staging.nation;
DROP TABLE IF EXISTS local.tpch1_staging.region;
DROP SCHEMA IF EXISTS local.tpch1_staging;

CREATE SCHEMA local.tpch1_staging
WITH (location = 's3://trino/staging/sf1/');

CREATE TABLE local.tpch1_staging.lineitem (
    l_orderkey      BIGINT,
    l_partkey       BIGINT,
    l_suppkey       BIGINT,
    l_linenumber    INTEGER,
    l_quantity      DECIMAL(15,2),
    l_extendedprice DECIMAL(15,2),
    l_discount      DECIMAL(15,2),
    l_tax           DECIMAL(15,2),
    l_returnflag    VARCHAR,
    l_linestatus    VARCHAR,
    l_shipdate      DATE,
    l_commitdate    DATE,
    l_receiptdate   DATE,
    l_shipinstruct  VARCHAR,
    l_shipmode      VARCHAR,
    l_comment       VARCHAR
) WITH (
    format            = 'PARQUET',
    external_location = 's3://trino/staging/sf1/lineitem'
);

CREATE TABLE local.tpch1_staging.orders (
    o_orderkey      BIGINT,
    o_custkey       BIGINT,
    o_orderstatus   VARCHAR,
    o_totalprice    DECIMAL(15,2),
    o_orderdate     DATE,
    o_orderpriority VARCHAR,
    o_clerk         VARCHAR,
    o_shippriority  INTEGER,
    o_comment       VARCHAR
) WITH (
    format            = 'PARQUET',
    external_location = 's3://trino/staging/sf1/orders'
);

CREATE TABLE local.tpch1_staging.customer (
    c_custkey    BIGINT,
    c_name       VARCHAR,
    c_address    VARCHAR,
    c_nationkey  BIGINT,
    c_phone      VARCHAR,
    c_acctbal    DECIMAL(15,2),
    c_mktsegment VARCHAR,
    c_comment    VARCHAR
) WITH (
    format            = 'PARQUET',
    external_location = 's3://trino/staging/sf1/customer'
);

CREATE TABLE local.tpch1_staging.partsupp (
    ps_partkey    BIGINT,
    ps_suppkey    BIGINT,
    ps_availqty   INTEGER,
    ps_supplycost DECIMAL(15,2),
    ps_comment    VARCHAR
) WITH (
    format            = 'PARQUET',
    external_location = 's3://trino/staging/sf1/partsupp'
);

CREATE TABLE local.tpch1_staging.part (
    p_partkey     BIGINT,
    p_name        VARCHAR,
    p_mfgr        VARCHAR,
    p_brand       VARCHAR,
    p_type        VARCHAR,
    p_size        INTEGER,
    p_container   VARCHAR,
    p_retailprice DECIMAL(15,2),
    p_comment     VARCHAR
) WITH (
    format            = 'PARQUET',
    external_location = 's3://trino/staging/sf1/part'
);

CREATE TABLE local.tpch1_staging.supplier (
    s_suppkey   BIGINT,
    s_name      VARCHAR,
    s_address   VARCHAR,
    s_nationkey BIGINT,
    s_phone     VARCHAR,
    s_acctbal   DECIMAL(15,2),
    s_comment   VARCHAR
) WITH (
    format            = 'PARQUET',
    external_location = 's3://trino/staging/sf1/supplier'
);

CREATE TABLE local.tpch1_staging.nation (
    n_nationkey BIGINT,
    n_name      VARCHAR,
    n_regionkey BIGINT,
    n_comment   VARCHAR
) WITH (
    format            = 'PARQUET',
    external_location = 's3://trino/staging/sf1/nation'
);

CREATE TABLE local.tpch1_staging.region (
    r_regionkey BIGINT,
    r_name      VARCHAR,
    r_comment   VARCHAR
) WITH (
    format            = 'PARQUET',
    external_location = 's3://trino/staging/sf1/region'
);

-- Sanity check
SELECT 'lineitem' AS tbl, count(*) AS cnt, count(l_orderkey)  AS non_null_key FROM local.tpch1_staging.lineitem
UNION ALL SELECT 'orders',   count(*), count(o_orderkey)  FROM local.tpch1_staging.orders
UNION ALL SELECT 'customer', count(*), count(c_custkey)   FROM local.tpch1_staging.customer
UNION ALL SELECT 'partsupp', count(*), count(ps_partkey)  FROM local.tpch1_staging.partsupp
UNION ALL SELECT 'part',     count(*), count(p_partkey)   FROM local.tpch1_staging.part
UNION ALL SELECT 'supplier', count(*), count(s_suppkey)   FROM local.tpch1_staging.supplier
UNION ALL SELECT 'nation',   count(*), count(n_nationkey) FROM local.tpch1_staging.nation
UNION ALL SELECT 'region',   count(*), count(r_regionkey) FROM local.tpch1_staging.region;

-- Load the data into bucketed orc files
DROP TABLE IF EXISTS local.tpch1.lineitem;
DROP TABLE IF EXISTS local.tpch1.orders;
DROP TABLE IF EXISTS local.tpch1.customer;
DROP TABLE IF EXISTS local.tpch1.partsupp;
DROP TABLE IF EXISTS local.tpch1.part;
DROP TABLE IF EXISTS local.tpch1.supplier;
DROP TABLE IF EXISTS local.tpch1.nation;
DROP TABLE IF EXISTS local.tpch1.region;
DROP SCHEMA IF EXISTS local.tpch1;

CREATE SCHEMA local.tpch1;

CREATE TABLE local.tpch1.lineitem
WITH (format = 'ORC', bucketed_by = ARRAY['l_orderkey'], bucket_count = 32)
AS SELECT *
FROM local.tpch1_staging.lineitem;

CREATE TABLE local.tpch1.orders
WITH (format = 'ORC', bucketed_by = ARRAY['o_orderkey'], bucket_count = 32)
AS SELECT *
FROM local.tpch1_staging.orders;

CREATE TABLE local.tpch1.customer
WITH (format = 'ORC', bucketed_by = ARRAY['c_custkey'], bucket_count = 32)
AS SELECT *
FROM local.tpch1_staging.customer;

CREATE TABLE local.tpch1.partsupp
WITH (format = 'ORC', bucketed_by = ARRAY['ps_partkey'], bucket_count = 32)
AS SELECT *
FROM local.tpch1_staging.partsupp;

CREATE TABLE local.tpch1.part
WITH (format = 'ORC', bucketed_by = ARRAY['p_partkey'], bucket_count = 32)
AS SELECT *
FROM local.tpch1_staging.part;

CREATE TABLE local.tpch1.supplier
WITH (format = 'ORC', bucketed_by = ARRAY['s_suppkey'], bucket_count = 32)
AS SELECT *
FROM local.tpch1_staging.supplier;

CREATE TABLE local.tpch1.nation
WITH (format = 'ORC')
AS SELECT *
FROM local.tpch1_staging.nation;

CREATE TABLE local.tpch1.region
WITH (format = 'ORC')
AS SELECT *
FROM local.tpch1_staging.region;


-- sanity check
SELECT 'lineitem' AS tbl, count(*) FROM local.tpch1.lineitem
UNION ALL SELECT 'orders',   count(*) FROM local.tpch1.orders
UNION ALL SELECT 'customer', count(*) FROM local.tpch1.customer
UNION ALL SELECT 'partsupp', count(*) FROM local.tpch1.partsupp
UNION ALL SELECT 'part',     count(*) FROM local.tpch1.part
UNION ALL SELECT 'supplier', count(*) FROM local.tpch1.supplier
UNION ALL SELECT 'nation',   count(*) FROM local.tpch1.nation
UNION ALL SELECT 'region',   count(*) FROM local.tpch1.region;

SELECT * FROM local.tpch1.lineitem  LIMIT 3;
SELECT * FROM local.tpch1.orders    LIMIT 3;
SELECT * FROM local.tpch1.customer  LIMIT 3;
SELECT * FROM local.tpch1.partsupp  LIMIT 3;
SELECT * FROM local.tpch1.part      LIMIT 3;
SELECT * FROM local.tpch1.supplier  LIMIT 3;
SELECT * FROM local.tpch1.nation    LIMIT 3;
SELECT * FROM local.tpch1.region    LIMIT 3;

-- Remove staging again
DROP TABLE local.tpch1_staging.lineitem;
DROP TABLE local.tpch1_staging.orders;
DROP TABLE local.tpch1_staging.customer;
DROP TABLE local.tpch1_staging.partsupp;
DROP TABLE local.tpch1_staging.part;
DROP TABLE local.tpch1_staging.supplier;
DROP TABLE local.tpch1_staging.nation;
DROP TABLE local.tpch1_staging.region;
DROP SCHEMA local.tpch1_staging;
