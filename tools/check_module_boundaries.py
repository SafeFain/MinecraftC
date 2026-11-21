#!/usr/bin/env python3
"""Check CMake module ownership, dependency direction and header consumers."""

from collections import defaultdict, deque
from pathlib import Path
import re
import sys

from check_api_boundaries import scan_cpp, source_files
from check_module_dependencies import resolve_include


SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".m", ".mm"}
HEADER_SUFFIXES = {".h", ".hh", ".hpp", ".hxx", ".inl", ".inc"}
TARGET_NAME = re.compile(r"\b(?:minecraftc(?:_[a-zA-Z0-9_]+)?|main)\b")


def inside(path, directory):
    try:
        return path.relative_to(directory)
    except ValueError:
        return None


def check(root, manifest, header_manifest):
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
            if path is None or path.suffix not in SOURCE_SUFFIXES | HEADER_SUFFIXES:
                continue
            relative = f"src/{path.as_posix()}"
            if not layer and path.suffix in SOURCE_SUFFIXES:
                problems.append(f"{name}: assign a dependency layer for {relative}")
            if not (root / relative).is_file():
                problems.append(f"{name}: source does not exist: {relative}")
            owners[relative].append(name)

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

    files = {path.relative_to(root).as_posix(): path for path in source_files(root)}
    known_files = set(files)
    header_rules = {}
    by_name = defaultdict(list)
    for row in header_manifest.read_text(encoding="utf-8").splitlines():
        header, owner, consumers = row.split("\t")
        if header in header_rules:
            problems.append(f"{header}: duplicate header boundary")
            continue
        if header not in files or not header.startswith("src/") or Path(header).suffix not in HEADER_SUFFIXES:
            problems.append(f"{header}: boundary must name an existing production header")
        if owner not in modules or owners[header] != [owner]:
            problems.append(f"{header}: expected one production owner {owner}, got {owners[header]}")
        allowed = tuple(filter(None, consumers.split(";")))
        for consumer in allowed:
            if consumer.endswith("/"):
                exists = (root / consumer).is_dir()
            else:
                exists = consumer in files
            if not exists or not consumer.startswith(("src/", "tests/")):
                problems.append(f"{header}: invalid header consumer {consumer}")
        header_rules[header] = allowed
        by_name[Path(header).name.casefold()].append(header)
    if not header_rules:
        problems.append("missing header boundaries")

    source_count = 0
    for relative, file in sorted(files.items()):
        if relative.startswith("src/") and file.suffix in SOURCE_SUFFIXES:
            if len(owners[relative]) != 1:
                problems.append(f"{relative}: expected one production owner, got {owners[relative]}")
            else:
                source_count += 1
        _, _, includes = scan_cpp(file.read_text(encoding="utf-8-sig"))
        for line, include in includes:
            spelling = include.replace("\\", "/")
            target = resolve_include(relative, spelling, known_files)
            # Compiler include paths can expose a protected header by basename.
            candidates = (target,) if target else by_name[Path(spelling).name.casefold()]
            for header in candidates:
                if header not in header_rules:
                    continue
                if not any(relative.startswith(path) if path.endswith("/") else relative == path
                           for path in header_rules[header]):
                    problems.append(f"{relative}:{line}: include crosses the {header} boundary")
    return problems, source_count


if __name__ == "__main__":
    failures, source_count = check(Path(sys.argv[1]).resolve(), Path(sys.argv[2]), Path(sys.argv[3]))
    if failures:
        sys.exit("\n".join(failures))
    print(f"Module dependency boundaries passed for {source_count} production sources")
