import traceback
from multiprocessing import Pool
from typing import Callable, Optional, TypeVar

T = TypeVar("T")
R = TypeVar("R")


def _safe_call(args):
    fn, item = args
    try:
        return fn(item), None
    except Exception as e:
        return None, (item, e)


def parallel_map(
    fn: Callable[[T], R],
    items: list[T],
    *,
    workers: Optional[int] = None,
    label: str = "items",
) -> list[R]:
    if workers == 1:
        pairs = [_safe_call((fn, item)) for item in items]
    else:
        with Pool(processes=workers) as pool:
            pairs = pool.map(_safe_call, [(fn, item) for item in items])

    results = []
    for value, err in pairs:
        if err is not None:
            item, e = err
            # print(f"failed to process {item}: {e}")
            # print()
            traceback.print_exception(type(e), e, e.__traceback__)
        else:
            results.append(value)

    print(f"processed {len(results)}/{len(items)} {label}")
    return results


def q_error(a, b) -> float:
    return max(a / b, b / a)
