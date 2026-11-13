from ._core import json_decode
from importlib.metadata import version, PackageNotFoundError

try:
    __version__ = version("specific-json")
except PackageNotFoundError:
    __version__ = "0.1.2"

__all__ = ('json_decode', )