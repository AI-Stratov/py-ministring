import random

import pytest
from ministring import ministr

# 1, 2, 3 and 4 bytes in UTF-8
CHARS = ["a", "é", "€", "😀"]


def random_text(n, seed):
    rng = random.Random(seed)
    return "".join(rng.choice(CHARS) for _ in range(n))


# Lengths around the index stride (64) and its multiples
TEXTS = {f"mixed-{n}": random_text(n, n) for n in (1, 2, 63, 64, 65, 127, 128, 129, 200)}
TEXTS.update({
    "ascii": "a" * 130,
    "wide-last": "a" * 130 + "😀",
    "wide-first": "😀" + "a" * 130,
    "all-4-byte": "😀" * 130,
})


@pytest.mark.parametrize("text", TEXTS.values(), ids=TEXTS.keys())
def test_indexing_matches_str(text):
    s = ministr(text)
    assert len(s) == len(text)
    for i in range(-len(text), len(text)):
        assert s[i] == text[i]


@pytest.mark.parametrize("text", TEXTS.values(), ids=TEXTS.keys())
def test_slicing_matches_str(text):
    s = ministr(text)
    bounds = range(-len(text) - 2, len(text) + 3)
    for i in bounds:
        for j in bounds:
            assert s[i:j] == text[i:j]
