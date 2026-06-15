select sum(l_extendedprice * (1 - l_discount)) as revenue
from lineitem, orders, customer
where l_orderkey = o_orderkey and o_custkey = c_custkey and c_nationkey >= 6
