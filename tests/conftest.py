"""Load the HA test harness when available (CI)."""

import importlib.util

if importlib.util.find_spec("pytest_homeassistant_custom_component"):
    pytest_plugins = ["pytest_homeassistant_custom_component"]
