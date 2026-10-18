"""Load authoring dictionaries with package-owned asset imports."""
import copy
import json
from pathlib import Path

ASSETS = Path(__file__).resolve().parents[1] / "assets"


def _import_path(name):
    path = Path(name)
    if path.is_absolute() or ".." in path.parts:
        raise ValueError("definition imports must stay inside the asset root")
    result = (ASSETS / path).resolve()
    if not result.is_relative_to(ASSETS.resolve()):
        raise ValueError("definition import escapes the asset root")
    return result


def _raw(path):
    try:
        data = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise ValueError(f"cannot load definition {path}: {error}") from error
    if not isinstance(data, dict):
        raise ValueError(f"definition {path} must be an object")
    return data


def load_definition(path, stack=()):
    path = Path(path).resolve()
    if path in stack or len(stack) >= 16:
        raise ValueError("definition import cycle/depth limit")
    data = _raw(path)
    imports = data.pop("imports", [])
    order = data.pop("entry_order", {})
    if not isinstance(imports, list) or not isinstance(order, dict):
        raise ValueError("invalid definition imports/order")
    for name in imports:
        imported = load_definition(_import_path(name), (*stack, path))
        for section, values in imported.items():
            if section not in data:
                data[section] = copy.deepcopy(values)
            elif isinstance(values, dict) and isinstance(data[section], dict):
                for key, value in values.items():
                    if key in data[section]:
                        raise ValueError(f"duplicate imported definition {section}.{key}")
                    data[section][key] = copy.deepcopy(value)
            elif data[section] != values:
                raise ValueError(f"conflicting imported definition {section}")
    for section, names in order.items():
        if not isinstance(names, list) or len(set(names)) != len(names):
            raise ValueError("invalid definition entry order")
        values = data.get(section, {})
        if not isinstance(values, dict):
            raise ValueError("ordered definition section must be an object")
        data[section] = {**{key: values[key] for key in names if key in values},
                         **{key: value for key, value in values.items() if key not in names}}
    return data


def write_owned_definition(path, document):
    """Keep edits in the original owning dictionary rather than flattening imports."""
    root = Path(path).resolve()
    files = {}

    def visit(current, stack=()):
        if current in stack or len(stack) >= 16:
            raise ValueError("definition import cycle/depth limit")
        files[current] = _raw(current)
        for name in files[current].get("imports", []):
            visit(_import_path(name), (*stack, current))

    visit(root)
    for section, values in document.items():
        if isinstance(values, dict):
            owners = {key: owner for owner, raw in files.items()
                      if isinstance(raw.get(section), dict) for key in raw[section]}
            for owner, raw in files.items():
                if isinstance(raw.get(section), dict):
                    raw[section] = {key: copy.deepcopy(value) for key, value in values.items()
                                    if owners.get(key, root) == owner}
        elif section not in ("imports", "entry_order"):
            files[root][section] = copy.deepcopy(values)
    for current, raw in files.items():
        current.write_text(json.dumps(raw, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
