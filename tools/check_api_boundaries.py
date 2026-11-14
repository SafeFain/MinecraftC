#!/usr/bin/env python3
"""Reject platform or graphics APIs outside their designated adapters."""

from __future__ import annotations

import argparse
from bisect import bisect_right
import json
import pathlib
import re
import sys


RULES = {
    "sdl": re.compile(r"(?:#\s*include\s*[<\"]SDL3/|\bSDL_[A-Za-z0-9_]+)"),
    "opengl": re.compile(
        r"(?:#\s*include\s*[<\"]glad/|\bgl[A-Z][A-Za-z0-9_]*\s*\(|"
        r"\bGL(?:uint|int|enum|sizei|boolean|sizeiptr|intptr)\b|\bGL_[A-Z0-9_]+)"
    ),
    "vulkan": re.compile(
        r"(?:#\s*include\s*[<\"]vulkan/|\bvk[A-Z][A-Za-z0-9_]*\s*\(|"
        r"\bPFN_vk[A-Za-z0-9_]*\b|\bVk[A-Z][A-Za-z0-9_]*\b|\bVK_[A-Z0-9_]+)"
    ),
    "native": re.compile(
        r"(?:#\s*include\s*[<\"](?:windows\.h|shlobj\.h|unistd\.h|pwd\.h|"
        r"mach-o/dyld\.h|winsock2\.h|ws2tcpip\.h|sys/socket\.h|sys/select\.h|"
        r"netinet/(?:in|tcp)\.h|arpa/inet\.h|poll\.h|dns_sd\.h|ifaddrs\.h|net/if\.h|iphlpapi\.h|jni\.h|mdns\.h)[>\"]|"
        r"\b(?:WSAStartup|WSACleanup|WSAGetLastError|closesocket|ioctlsocket|"
        r"inet_pton|inet_ntop|setsockopt|getsockopt|GetAdaptersAddresses|getifaddrs|freeifaddrs|"
        r"DNSService[A-Za-z0-9_]+|mdns_[A-Za-z0-9_]+|GetModuleFileNameW|SHGetKnownFolderPath|"
        r"MoveFileExW|getpwuid_r|readlink)\s*\()"
    ),
}

# Lexical filtering is intentionally independent of build configuration: code in
# an inactive platform branch must obey the same ownership rules. Keep includes
# (including macro-expanded headers), but ignore prose and shader strings.
LEXEMES = re.compile(
    r'(?P<comment>//[^\n]*|/\*[\s\S]*?\*/)'
    r'|(?P<raw>(?:u8|u|U|L)?R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\([\s\S]*?\)(?P=delimiter)")'
    r'|(?P<string>(?:u8|u|U|L)?"(?:\\[\s\S]|[^"\\])*"|(?<![\w])(?:u8|u|U|L)?\'(?:\\[^\r\n]|[^\'\\\r\n])*\')'
)
HEADERS = {
    "sdl": re.compile(r"SDL3/", re.I),
    "opengl": re.compile(r"(?:glad/|GL/|GLES[0-9]?/|OpenGL/)", re.I),
    "vulkan": re.compile(r"(?:vulkan/|vk_mem_alloc\.h$)", re.I),
    "vulkan_internal": re.compile(r"(?:.*/)?VulkanRendererInternal\.h$", re.I),
    "native": re.compile(
        r"(?:windows|shlobj|unistd|pwd|winsock2|ws2tcpip|poll|dns_sd|ifaddrs|iphlpapi|jni|mdns)\.h$"
        r"|(?:mach-o/dyld|sys/(?:socket|select)|netinet/(?:in|tcp)|arpa/inet|net/if)\.h$", re.I),
}


def dependencies(source: str):
    splice_offsets = []
    removed = 0
    for splice in re.finditer(r"\\\r?\n", source):
        splice_offsets.append(splice.start() - removed)
        removed += len(splice.group())
    source = re.sub(r"\\\r?\n", "", source)
    def source_line(offset):
        return source.count("\n", 0, offset) + 1 + bisect_right(splice_offsets, offset)
    literals = {}

    def mask(match):
        # Preserve offsets/newlines so diagnostics point to the offending source.
        if match.lastgroup == "string":
            literals[match.start()] = match.group()
        return re.sub(r"[^\n]", " ", match.group())

    code = LEXEMES.sub(mask, source)
    macros = {}
    for directive in re.finditer(r"^[ \t]*#[ \t]*(define|include)\b[^\n]*", code, re.M):
        characters = list(directive.group())
        for offset, literal in literals.items():
            if directive.start() <= offset < directive.end():
                start = offset - directive.start()
                characters[start:start + len(literal)] = literal
        original = "".join(characters).strip()
        if directive.group(1) == "define":
            definition = re.match(r"#\s*define\s+(\w+)\s+(.+)", original)
            if definition:
                macros[definition[1]] = definition[2].strip()
            continue
        argument = re.sub(r"^#\s*include\s*", "", original).strip()
        seen = set()
        while argument in macros and argument not in seen:
            seen.add(argument)
            argument = macros[argument]
        header = re.match(r'[<"]([^>"\n]+)[>"]', argument)
        if header:
            for rule, pattern in HEADERS.items():
                if pattern.match(header[1]):
                    yield rule, source_line(directive.start()), header[1]
    for rule, pattern in RULES.items():
        for match in pattern.finditer(code):
            yield rule, source_line(match.start()), match.group()

ALLOWED_PREFIXES = {
    "sdl": ("src/platform/sdl/",),
    "opengl": (),
    "vulkan": (
        "src/renderer/backend/vulkan/",
        "src/platform/sdl/SdlWindow.cpp",
    ),
    "native": ("src/platform/native/",),
    "vulkan_internal": ("src/renderer/backend/vulkan/",),
}


def source_files(root: pathlib.Path):
    for directory in ("src", "tests"):
        for path in (root / directory).rglob("*"):
            if path.is_file() and path.suffix.lower() in {".h", ".hpp", ".hh", ".hxx", ".c", ".cc", ".cpp", ".cxx", ".m", ".mm", ".inl", ".inc"}:
                yield path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=pathlib.Path, default=pathlib.Path.cwd())
    parser.add_argument(
        "--baseline", type=pathlib.Path,
        default=pathlib.Path("tools/api_boundary_baseline.json"))
    args = parser.parse_args()
    root = args.root.resolve()
    if not (root / "src").is_dir():
        parser.error(f"source directory does not exist: {root / 'src'}")
    baseline_path = args.baseline
    if not baseline_path.is_absolute():
        baseline_path = root / baseline_path
    baseline = json.loads(baseline_path.read_text(encoding="utf-8"))
    if any(baseline.values()):
        print("API boundary check failed: file-wide baseline exceptions are forbidden", file=sys.stderr)
        return 1
    errors: list[str] = []

    for path in source_files(root):
        relative = path.relative_to(root).as_posix()
        source = path.read_text(encoding="utf-8", errors="replace")
        for rule, line, token in dependencies(source):
            # Display/platform integration drivers may call adapter APIs. They
            # still cannot compile the renderer's implementation layout.
            if relative.startswith("tests/") and rule != "vulkan_internal":
                continue
            if any(relative.startswith(prefix) if prefix.endswith("/") else relative == prefix
                   for prefix in ALLOWED_PREFIXES[rule]):
                continue
            errors.append(f"{relative}:{line}: forbidden {rule} dependency: {token}")

    if errors:
        print("API boundary check failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    print("API boundary check passed: no forbidden dependencies")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
