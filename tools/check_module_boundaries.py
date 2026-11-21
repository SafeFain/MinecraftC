#!/usr/bin/env python3
"""Check production source ownership, CMake dependency direction and private headers."""

from collections import defaultdict, deque
from pathlib import Path
import re
import sys


SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".m", ".mm"}
PRIVATE_HEADERS = {
    "ContentMutation.h": ("plugins/",),
    "AtlasPublication.h": (
        "plugins/AtlasBindings.cpp",
        "plugins/PluginManager.cpp",
        "renderer/BlockAtlasData.cpp",
    ),
    "BlockTextureDefinitions.h": ("world/Block.cpp", "renderer/BlockAtlasData.cpp"),
    "VulkanRendererInternal.h": ("renderer/backend/vulkan/",),
    "VulkanUIRenderer.h": ("app/Application.cpp", "renderer/backend/vulkan/"),
    "GameRenderer.h": ("app/", "entity/", "player/", "renderer/", "ui/"),
}
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[">]', re.MULTILINE)
TARGET_NAME = re.compile(r"\b(?:minecraftc(?:_[a-zA-Z0-9_]+)?|main)\b")


def inside(path, directory):
    try:
        return path.relative_to(directory)
    except ValueError:
        return None


def check(root, manifest):
    problems = []
    owners = defaultdict(list)
    modules = {}
    src = root / "src"
    for line in manifest.read_text(encoding="utf-8").splitlines():
        name, layer, sources, links = line.split("\t")
        if layer:
            modules[name] = (int(layer), set(TARGET_NAME.findall(links)))
        for source in sources.split(";"):
            source = Path(source)
            path = inside((root / source).resolve(), src)
            if path is None or path.suffix not in SOURCE_SUFFIXES:
                continue
            if not layer:
                problems.append(f"{name}: assign a dependency layer for {path}")
            if not (src / path).is_file():
                problems.append(f"{name}: source does not exist: {path}")
            owners[path].append(name)

    for name, (layer, links) in modules.items():
        for dependency in sorted(links):
            if dependency in modules and modules[dependency][0] >= layer:
                problems.append(f"{name}: invalid dependency on {dependency}")
            elif dependency.startswith("minecraftc_") and dependency not in modules and dependency != "minecraftc_moltenvk":
                problems.append(f"{name}: unclassified dependency {dependency}")

    pending = deque([("minecraftc_ui",)])
    visited = {"minecraftc_ui"}
    while pending:
        path = pending.popleft()
        target = path[-1]
        if target == "minecraftc_render_vulkan":
            problems.append(f"UI links the Vulkan backend: {' -> '.join(path)}")
            break
        if target in modules:
            for dependency in sorted(modules[target][1] - visited):
                visited.add(dependency)
                pending.append((*path, dependency))

    for file in sorted(src.rglob("*")):
        path = file.relative_to(src)
        if file.suffix in SOURCE_SUFFIXES and len(owners[path]) != 1:
            problems.append(f"{path}: expected one production owner, got {owners[path]}")
        if not file.is_file() or file.suffix not in SOURCE_SUFFIXES | {".h", ".hpp", ".inl"}:
            continue
        for include in INCLUDE.findall(file.read_text(encoding="utf-8-sig")):
            allowed = PRIVATE_HEADERS.get(Path(include).name)
            if allowed and not any(
                path.as_posix().startswith(item) if item.endswith("/") else path.as_posix() == item
                for item in allowed
            ):
                problems.append(f"{path}: include crosses the {include} boundary")
    return problems, sum(len(entries) == 1 for entries in owners.values())


if __name__ == "__main__":
    failures, source_count = check(Path(sys.argv[1]).resolve(), Path(sys.argv[2]))
    if failures:
        sys.exit("\n".join(failures))
    print(f"Module dependency boundaries passed for {source_count} production sources")
