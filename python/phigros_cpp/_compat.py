from importlib import import_module

try:
    core = import_module("phigros_cpp._core")
except ModuleNotFoundError:
    core = import_module("_core")

__all__ = ["core"]
