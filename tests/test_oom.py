import os
import subprocess
import sys
import textwrap

import pytest
import ministring

_testcapi = pytest.importorskip("_testcapi")
if not hasattr(_testcapi, "set_nomemory"):
    pytest.skip("_testcapi.set_nomemory is not available", allow_module_level=True)

# _testcapi.set_nomemory(n) makes every allocation after the n-th one fail.
# Walking n upwards hits each allocation in turn. Each failure must surface
# as MemoryError, never as a crash.
CHILD = textwrap.dedent("""
    import _testcapi
    from ministring import ministr

    def ops():
        for text in ("hello world", "hello \\U0001F600 world", "\\u044f" * 200):
            s = ministr(text)
            s[len(s) // 2]
            s[-1]
            s[1:-1]
            list(s)
            hash(s)
            s == text

    for start in range({limit}):
        _testcapi.set_nomemory(start)
        try:
            ops()
        except MemoryError:
            continue
        finally:
            _testcapi.remove_mem_hooks()
        print("ok", start)
        break
""")


def test_allocation_failures_raise_memory_error():
    """Run in a child under -X dev: the debug allocator fills fresh memory
    with 0xCD, so freeing an uninitialized pointer crashes instead of
    passing by luck."""
    env = dict(os.environ, PYTHONPATH=os.path.dirname(ministring.__file__))
    proc = subprocess.run(
        [sys.executable, "-X", "dev", "-c", CHILD.format(limit=1000)],
        env=env, capture_output=True, text=True, timeout=120,
    )
    assert proc.returncode == 0, proc.stderr[-2000:]
    assert proc.stdout.startswith("ok"), proc.stdout
