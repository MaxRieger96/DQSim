select sum(l_extendedprice * (1 - l_discount)) as revenue
from lineitem
