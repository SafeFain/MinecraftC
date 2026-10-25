#!/usr/bin/env python3
"""Report source include dependencies and reject file-level include cycles.

The tool resolves quoted includes in the order used by the module-coupling
analysis: relative to the including file, then relative to src/, then relative
to the repository root. Only files that resolve under src/ contribute edges, so
SDK, vendored and generated headers stay out of the graph.

It is read-only. --report prints the measurements recorded by the decoupling
task; --check-cycles fails when the file-level include graph contains a cycle.
Contract-specific include rules are introduced by a later build-boundary task.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys
from collections import defaultdict

EXTENSIONS = {".h", ".hpp", ".c", ".cpp", ".mm"}
TU_EXTENSIONS = {".c", ".cpp", ".mm"}
INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"')
# Public contracts that the decoupling milestones measure. New files simply
# report "missing" until the milestone that introduces them lands.
DEFAULT_HEADERS = (
    "Config.h",
    "world/Block.h",
    "game/Item.h",
    "plugins/Extensions.h",
    "world/ChunkMesh.h",
    "world/ChunkMeshBuilder.h",
    "plugins/ContentRegistry.h",
    "plugins/ContentTypes.h",
    "plugins/ContentQueries.h",
    "game/SaveStore.h",
    "world/World.h",
    "entity/EntityRecord.h",
    "entity/EntityAttack.h",
    "entity/EntityPresentation.h",
    "entity/EntityManager.h",
    "app/GameSession.h",
)


def source_files(root: pathlib.Path) -> list[str]:
    paths = []
    for path in (root / "src").rglob("*"):
        if path.suffix in EXTENSIONS and path.is_file():
            paths.append(path.relative_to(root).as_posix())
    return sorted(paths)


def resolve_include(including: str, name: str, known: set[str]) -> str | None:
    parent = pathlib.PurePosixPath(including).parent
    candidates = [
        (parent / name).as_posix(),
        f"src/{name}",
        name,
    ]
    for candidate in candidates:
        normalized = pathlib.PurePosixPath(candidate).as_posix()
        if normalized in known:
            return normalized
    return None


def build_graph(root: pathlib.Path) -> dict[str, list[str]]:
    files = source_files(root)
    known = set(files)
    graph: dict[str, list[str]] = {}
    for relative in files:
        path = root / relative
        targets: list[str] = []
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            text = ""
        for line in text.splitlines():
            match = INCLUDE_RE.match(line)
            if not match:
                continue
            target = resolve_include(relative, match.group(1), known)
            if target is not None and target not in targets:
                targets.append(target)
        graph[relative] = sorted(targets)
    return graph


def module_of(relative: str) -> str:
    parts = relative.split("/")
    if len(parts) == 2:
        return parts[1].rsplit(".", 1)[0]
    if len(parts) >= 3:
        return parts[1]
    return "unresolved"


def reachable(graph: dict[str, list[str]], start: str) -> set[str]:
    seen: set[str] = set()
    stack = [start]
    while stack:
        current = stack.pop()
        for target in graph.get(current, ()):  # missing nodes have no edges
            if target not in seen:
                seen.add(target)
                stack.append(target)
    return seen


def find_cycles(graph: dict[str, list[str]]) -> list[list[str]]:
    """Return strongly connected components larger than one file."""
    index_counter = 0
    indices: dict[str, int] = {}
    low: dict[str, int] = {}
    on_stack: set[str] = set()
    stack: list[str] = []
    components: list[list[str]] = []

    def strongconnect(node: str) -> None:
        nonlocal index_counter
        indices[node] = index_counter
        low[node] = index_counter
        index_counter += 1
        stack.append(node)
        on_stack.add(node)
        for target in graph.get(node, ()):
            if target not in indices:
                strongconnect(target)
                low[node] = min(low[node], low[target])
            elif target in on_stack:
                low[node] = min(low[node], indices[target])
        if low[node] == indices[node]:
            component = []
            while True:
                member = stack.pop()
                on_stack.discard(member)
                component.append(member)
                if member == node:
                    break
            if len(component) > 1:
                components.append(sorted(component))

    sys.setrecursionlimit(max(10000, 4 * len(graph) + 100))
    for node in sorted(graph):
        if node not in indices:
            strongconnect(node)
    return sorted(components)


def header_table(graph: dict[str, list[str]], headers: list[str]) -> None:
    files = list(graph)
    tus = [f for f in files if pathlib.PurePosixPath(f).suffix in TU_EXTENSIONS]
    for header in headers:
        path = f"src/{header}"
        if path not in graph:
            print(f"  {header:<38} missing")
            continue
        direct = sum(1 for targets in graph.values() if path in targets)
        reach = sum(1 for tu in tus if path in reachable(graph, tu))
        closure = reachable(graph, path)
        print(f"  {header:<38} direct {direct:3d}  TUs {reach:3d}  closure {len(closure):3d}")


def report(root: pathlib.Path, graph: dict[str, list[str]], headers: list[str]) -> None:
    files = list(graph)
    tus = [f for f in files if pathlib.PurePosixPath(f).suffix in TU_EXTENSIONS]
    edges = sum(len(targets) for targets in graph.values())
    cross = 0
    pairs: set[tuple[str, str]] = set()
    for source, targets in graph.items():
        source_module = module_of(source)
        for target in targets:
            target_module = module_of(target)
            if source_module != target_module:
                cross += 1
                pairs.add((source_module, target_module))

    print(f"root: {root}")
    print(f"source/header files: {len(files)}")
    print(f"source translation units: {len(tus)}")
    print(f"direct file dependencies: {edges}")
    print(f"cross-module direct dependencies: {cross} ({cross * 100.0 / max(edges, 1):.1f}%)")
    print(f"distinct directed module pairs: {len(pairs)}")

    cycles = find_cycles(graph)
    print(f"file-level include cycles: {len(cycles)}")
    for component in cycles[:10]:
        print("  cycle:", ", ".join(component))

    modules = sorted({module_of(f) for f in files})
    counts = defaultdict(lambda: [0, 0])
    for source, targets in graph.items():
        module = module_of(source)
        counts[module][0] += sum(1 for t in targets if module_of(t) != module)
        for target in targets:
            if module_of(target) != module:
                counts[module_of(target)][1] += 1
    print("\nmodule                out/in (cross-module file dependencies)")
    for module in modules:
        out_count, in_count = counts[module]
        print(f"  {module:<18} {out_count:4d} / {in_count:4d}")

    print("\nheader contract propagation (direct includers / transitive TUs / closure files):")
    header_table(graph, headers)


def print_closure(graph: dict[str, list[str]], header: str) -> None:
    path = f"src/{header}"
    if path not in graph:
        print(f"closure of {header}: missing")
        return
    closure = sorted(reachable(graph, path))
    print(f"closure of {header} ({len(closure)} files):")
    for entry in closure:
        print(f"  {entry}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=pathlib.Path, default=pathlib.Path.cwd())
    parser.add_argument("--report", action="store_true",
                        help="print the dependency report (default action)")
    parser.add_argument("--check-cycles", action="store_true",
                        help="exit non-zero when a file-level include cycle exists")
    parser.add_argument("--headers", default="",
                        help="comma-separated src-relative headers for the propagation table")
    parser.add_argument("--print-closure", action="append", default=[],
                        help="print every file reachable from this src-relative header")
    args = parser.parse_args()

    root = args.root.resolve()
    graph = build_graph(root)
    if not graph:
        print(f"no source files found under {root / 'src'}", file=sys.stderr)
        return 1

    if args.report or not args.check_cycles:
        headers = [h.strip() for h in args.headers.split(",") if h.strip()]
        report(root, graph, headers or list(DEFAULT_HEADERS))
    for header in args.print_closure:
        print_closure(graph, header)

    if args.check_cycles:
        cycles = find_cycles(graph)
        if cycles:
            print(f"Include cycle check failed: {len(cycles)} component(s)", file=sys.stderr)
            for component in cycles:
                print("  - " + ", ".join(component), file=sys.stderr)
            return 1
        print("Include cycle check passed: the file-level include graph is acyclic")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
