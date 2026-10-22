"""Bytes per character: str vs ministr, before and after the first s[i].

Uses tracemalloc, so the ministr columns include everything ministr() and
s[i] allocate, including anything they might add to the source str.

    python benchmarks/memory.py
"""
import sys
import tracemalloc

from ministring import ministr

N = 1_000_000

CASES = {
    "ASCII": "a" * N,
    "ASCII + one emoji": "a" * (N - 1) + "\U0001F600",
    "ASCII + one €": "a" * (N - 1) + "€",
    "Latin-1 (café)": "café" * (N // 4),
    "Cyrillic": "я" * N,
    "CJK": "世" * N,
}


def allocated(fn):
    tracemalloc.start()
    try:
        result = fn()
        size, _ = tracemalloc.get_traced_memory()
    finally:
        tracemalloc.stop()
    return size, result


def main():
    print("| Text | `str` | `ministr` | `ministr` after `s[0]` |")
    print("|---|---|---|---|")
    for name, text in CASES.items():
        text = (text + ".")[:-1]  # a fresh copy with no cached UTF-8
        n = len(text)
        str_size = sys.getsizeof(text)
        created, s = allocated(lambda: ministr(text))
        indexed, _ = allocated(lambda: s[0])
        print(f"| {name} | {str_size / n:.2f} | {created / n:.2f} | {(created + indexed) / n:.2f} |")


if __name__ == "__main__":
    main()
