from __future__ import annotations
from typing import Any


def json_decode(json_str: str | bytes, *attrs: str | int, default: Any =None) -> Any:
    """
    Decode a json object into a python object.
    Raises

    Parameters
    -----------
    json_str: `str` | `bytes`
        The json object to decode

    *attrs: `str` | `int`
        A tuple of string keys/index. For example, you have a json object like so:
        `{"key": ["python", "c"]}`

        If `attrs` is `("key", "1")`, it will return "c".
        Note that strings are for dict/object keys and indexes are for arrays/lists.

        Raises a value error (or returns default) if no value could be found.

    default: `Any`
        This can only be set when `attrs` is also set.
        Returns this value if :exec:`ValueError` or :exec:`IndexError` is raised.
    """
