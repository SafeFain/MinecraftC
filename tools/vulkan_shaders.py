#!/usr/bin/env python3
"""Generate or validate MinecraftC's checked-in Vulkan SPIR-V shaders."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import shutil
import struct
import subprocess
import tempfile


SHADERS = ("basic_cube.vert", "basic_cube.frag", "chunk.vert", "chunk.frag",
           "shadow.vert", "shadow.frag",
           "ui.vert", "ui.frag", "weather.vert", "weather.frag",
           "sky.vert", "sky.frag", "cloud.vert", "cloud.frag",
           "wireframe.vert", "wireframe.frag", "model.vert", "model.frag",
           "post.vert", "post.frag", "bloom.frag", "screen_effect.frag",
           "screen_effect_gi.frag", "voxel_gi_inject.comp", "voxel_gi_probe.comp")
INCLUDES = ("screen_effect_common.glsl", "gi_surface_filter.glsl", "voxel_gi_aux.glsl", "voxel_gi_trace.glsl")
SPIRV_MAGIC = 0x07230203
SOURCE_MANIFEST = "sources.sha256.json"


def source_digest(path: pathlib.Path) -> str:
    # Git may expose text files with CRLF on Windows. Shader freshness is based
    # on source content, not the checkout's platform line-ending convention.
    normalized = path.read_bytes().replace(b"\r\n", b"\n")
    return hashlib.sha256(normalized).hexdigest()


def expected_source_manifest(shader_dir: pathlib.Path) -> dict[str, dict[str, str]]:
    return {
        "sources": {name: source_digest(shader_dir / name)
                    for name in SHADERS + INCLUDES},
        "binaries": {name + ".spv": hashlib.sha256(
            (shader_dir / (name + ".spv")).read_bytes()).hexdigest()
            for name in SHADERS},
    }


def validate_source_manifest(shader_dir: pathlib.Path) -> None:
    manifest_path = shader_dir / SOURCE_MANIFEST
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError(f"invalid Vulkan shader source manifest: {manifest_path}") from error
    expected = expected_source_manifest(shader_dir)
    if any(manifest.get(key) != value for key, value in expected.items()) or \
            not isinstance(manifest.get("compiler"), str):
        raise RuntimeError(
            "Vulkan shader sources or binaries changed without regenerating their manifest")


def compiler_version(glslc: str) -> str:
    return subprocess.check_output([glslc, "--version"], text=True).strip()


def write_source_manifest(shader_dir: pathlib.Path, glslc: str) -> None:
    manifest_path = shader_dir / SOURCE_MANIFEST
    manifest = expected_source_manifest(shader_dir)
    manifest["compiler"] = compiler_version(glslc)
    manifest_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def validate(shader_dir: pathlib.Path) -> None:
    for source_name in SHADERS:
        source = shader_dir / source_name
        binary = source.with_suffix(source.suffix + ".spv")
        if not source.is_file():
            raise RuntimeError(f"missing Vulkan shader source: {source}")
        data = binary.read_bytes()
        if len(data) < 20 or len(data) % 4 != 0:
            raise RuntimeError(f"invalid SPIR-V size: {binary}")
        if struct.unpack_from("<I", data)[0] != SPIRV_MAGIC:
            raise RuntimeError(f"invalid SPIR-V magic: {binary}")


def validate_compiled_shader(generated: pathlib.Path, checked_in: pathlib.Path,
                             matching_compiler: bool) -> None:
    data = generated.read_bytes()
    if matching_compiler and data != checked_in.read_bytes():
        raise RuntimeError(f"checked-in SPIR-V is stale: {checked_in.name}")
    if len(data) < 20 or len(data) % 4 != 0 or \
            struct.unpack_from("<I", data)[0] != SPIRV_MAGIC:
        raise RuntimeError(f"glslc generated invalid SPIR-V: {generated}")


def compile_shader(glslc: str, source: pathlib.Path, output: pathlib.Path) -> None:
    subprocess.run(
        [glslc, "--target-env=vulkan1.0", str(source), "-o", str(output)],
        check=True,
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=pathlib.Path, default=pathlib.Path.cwd())
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--generate", action="store_true")
    modes.add_argument("--check", action="store_true")
    parser.add_argument("--glslc", default="glslc")
    args = parser.parse_args()
    shader_dir = args.root.resolve() / "assets" / "shaders" / "vulkan"

    if not args.generate and not args.check:
        validate(shader_dir)
        validate_source_manifest(shader_dir)
        return 0

    glslc = shutil.which(args.glslc)
    if not glslc:
        raise RuntimeError(f"glslc was not found: {args.glslc}")
    if args.generate:
        for source_name in SHADERS:
            source = shader_dir / source_name
            compile_shader(glslc, source, source.with_suffix(source.suffix + ".spv"))
        write_source_manifest(shader_dir, glslc)
        validate(shader_dir)
        return 0

    # Different supported glslc releases can emit semantically equivalent but
    # bytewise different modules. Compile every source to catch platform-local
    # errors, and bind the source/include and checked-in binary hashes in one manifest.
    validate_source_manifest(shader_dir)
    manifest = json.loads((shader_dir / SOURCE_MANIFEST).read_text(encoding="utf-8"))
    matching_compiler = manifest["compiler"] == compiler_version(glslc)
    with tempfile.TemporaryDirectory(prefix="minecraftc-vulkan-shaders-") as temp:
        temp_dir = pathlib.Path(temp)
        for source_name in SHADERS:
            source = shader_dir / source_name
            generated = temp_dir / (source_name + ".spv")
            compile_shader(glslc, source, generated)
            validate_compiled_shader(generated, shader_dir / (source_name + ".spv"),
                                     matching_compiler)
    validate(shader_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
