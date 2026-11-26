"""Loads the apworld's Archipelago-free modules (tables, oracle, reach, seedout) for the scripts and tests that use
them, without running apworld/ship2ship/__init__.py (which needs Archipelago)."""
import importlib
import importlib.util
import pathlib
import sys

WORLD = pathlib.Path(__file__).resolve().parent.parent.parent / "apworld" / "ship2ship"
PURE_MODULES = ("tables", "oracle", "reach", "layout", "relevance", "seedout")


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def load_world_package():
    """The world's package with an empty __init__: its modules import each other, not Archipelago."""
    spec = importlib.util.spec_from_file_location("ship2ship", WORLD / "__init__.py",
                                                  submodule_search_locations=[str(WORLD)])
    sys.modules["ship2ship"] = importlib.util.module_from_spec(spec)
    return {name: importlib.import_module(f"ship2ship.{name}") for name in PURE_MODULES}
