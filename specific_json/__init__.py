from ._core import json_decode
from importlib.metadata import version, PackageNotFoundError

try:
    __version__ = version("specific_json")
except PackageNotFoundError:
    __version__ = "0.1.3"

__all__ = ('json_decode', )