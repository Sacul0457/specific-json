from __future__ import annotations
from typing import Any


def json_decode(json_str: str | bytes, /, *attrs: str | int,  default: Any = None) -> Any:
    """
    Decode a json object into a python object.

    **Note**: Values that are NOT needed to find `attrs` are not validated and hence can be invalid.
    For example, `{"key": ['value'], abc}` and `attrs` is `("key", 0)`. Even though `abc` is invalid,
    it will NOT be checked and no error will be raised.

    Parameters
    -----------
    json_str: `str` | `bytes`
        The json object to decode

    *attrs: `str` | `int`
        A tuple of string keys/index. For example, you have a json object like so:
        `{"key": ["python", "c"]}`

        If `attrs` is `("key", "1")`, it will return "c".
        Note that strings are for dict/object keys and indexes are for arrays/lists.

        Raises an error (or returns default) if no value could be found.

    default: `Any`
        This is only used when `attrs` is also set. Else, it is ignored.
        Returns this value if :exec:`KeyError` or :exec:`IndexError` is raised.
        It does **not** supress invalid JSON syntax.


    Raises
    -------
    :exec:`ValueError`
        - `json_str` is not a `str` or `bytes`
        -  Invalid JSON syntax is encountered.
        - `attrs` is not one of `str` or `int` if provided.
    :exec:`IndexError`
        - `attrs` provides an index out of range and a default is not provided.
    :exec:`KeyError`
        - `attrs` provides a key that is not found and a default is not provided.
    """
