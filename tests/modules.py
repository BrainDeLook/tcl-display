"""Load HA-independent modules without importing the integration entrypoint."""

import importlib.util
import sys
from pathlib import Path


def load(name):
    module_name = f"tcl_display_test_{name}"
    if module_name in sys.modules:
        return sys.modules[module_name]
    path = Path(__file__).parents[1] / "custom_components" / "tcl_display" / f"{name}.py"
    spec = importlib.util.spec_from_file_location(module_name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[module_name] = module
    spec.loader.exec_module(module)
    return module
