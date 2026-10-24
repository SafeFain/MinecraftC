"""Create and build self-contained MinecraftC content packages (Python stdlib only)."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

CATEGORIES = (
    "materials", "blocks", "block_families", "block_geometry", "items", "dimensions",
    "recipes", "entities", "generators", "block_behaviors", "item_behaviors",
    "loot_tables", "structures", "player_actions", "environments", "particles",
    "held_models", "playlists", "render_resources", "render_passes",
)
RESOURCES = ("textures", "models", "audio", "shaders", "lang")
LICENSES = ("LICENSE", "LICENSE.md", "LICENSE.txt", "NOTICE", "NOTICE.md", "NOTICE.txt")
_TOOL_PARENT = Path(__file__).resolve().parents[1]
SDK_DEFAULT = _TOOL_PARENT if (_TOOL_PARENT / "minecraftc/plugin.h").is_file() else _TOOL_PARENT / "sdk"


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def read_json(path):
    if path.stat().st_size > 16 * 1024 * 1024:
        raise ValueError(f"JSON exceeds size limit: {path}")
    return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique_object,
                      parse_constant=lambda value: (_ for _ in ()).throw(ValueError(value)))


def manifest(root):
    value = read_json(root / "mod.json")
    identifier = value.get("id", "")
    if not re.fullmatch(r"[a-z0-9_.-]+", identifier) or identifier == "minecraftc":
        raise ValueError("invalid/reserved package id")
    if identifier in (".", "..") or identifier.upper().split(".")[0] in (
            "CON", "PRN", "AUX", "NUL", *(f"COM{i}" for i in range(1, 10)),
            *(f"LPT{i}" for i in range(1, 10))):
        raise ValueError("package id is not a portable directory name")
    if (type(value.get("package_format")) is not int or value["package_format"] != 1
            or "data" in value or type(value.get("api")) is not int or value["api"] != 1):
        raise ValueError("expected package_format 1, api 1 and no data field")
    if not isinstance(value.get("version"), str):
        raise ValueError("package version is required")
    return value


def write_lf(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write(value)


def write_json(path, value):
    write_lf(path, json.dumps(value, ensure_ascii=False, indent=2) + "\n")


def safe_files(root):
    """Do not traverse links, including links in directories we would exclude."""
    if root.is_symlink():
        raise ValueError(f"package cannot be a symlink: {root}")
    for directory, dirs, files in os.walk(root, followlinks=False):
        for name in sorted(dirs + files):
            path = Path(directory) / name
            if path.is_symlink() or getattr(path, "is_junction", lambda: False)():
                raise ValueError(f"package cannot contain links: {path}")
        for name in sorted(files):
            yield Path(directory) / name


def runtime_files(root):
    result = []
    for path in safe_files(root):
        relative = path.relative_to(root)
        if (relative.as_posix() == "mod.json" or relative.as_posix() in LICENSES
                or (relative.parts[0] in CATEGORIES and path.suffix == ".json")
                or relative.parts[0] in RESOURCES):
            result.append(path)
    return sorted(result, key=lambda path: path.relative_to(root).as_posix())


def stage(root, output, module=None, builtin=False, refresh=False):
    value = manifest(root)
    if root.is_symlink() or getattr(root, "is_junction", lambda: False)():
        raise ValueError("source cannot be a link")
    if output.is_symlink() or getattr(output, "is_junction", lambda: False)():
        raise ValueError("output cannot be a link")
    root = root.resolve()
    output = output.resolve()
    if output == root or root in output.parents or output in root.parents:
        raise ValueError("output and source must be separate, non-nested directories")
    files = runtime_files(root)
    if output.exists() and not refresh:
        raise ValueError(f"output already exists: {output}")
    if refresh and output.exists():
        # Only the internal CMake staging command may replace an exact owned path.
        marker = output.parent / ("." + output.name + ".content-package-stage")
        if not marker.is_file() or marker.read_text(encoding="utf-8") != str(root):
            raise ValueError(f"refusing to replace unowned staging directory: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=".content-package-", dir=output.parent))
    try:
        for path in files:
            destination = temporary / path.relative_to(root)
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, destination)
        if module:
            platform = {"win32": "windows", "darwin": "macos"}.get(sys.platform, "linux")
            relative = Path(value["native"][platform])
            if relative.is_absolute() or ".." in relative.parts or ":" in str(relative):
                raise ValueError("invalid native module path")
            destination = temporary / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(module, destination)
        if builtin:
            inventory = {p.relative_to(temporary).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                         for p in sorted(temporary.rglob("*")) if p.is_file()}
            if len(inventory) > 4096 or any((temporary / name).stat().st_size > 32 * 1024 * 1024 for name in inventory):
                raise ValueError("builtin resource inventory/file exceeds mobile limits")
            if sum((temporary / name).stat().st_size for name in inventory) > 128 * 1024 * 1024:
                raise ValueError("builtin resources exceed mobile package limit")
            write_json(temporary / "resources.sha256.json", {"version": 1, "sha256": inventory})
        if output.exists():
            # output is absolute, separate from source, and ownership was checked above.
            shutil.rmtree(output)
        temporary.rename(output)
        if refresh:
            (output.parent / ("." + output.name + ".content-package-stage")).write_text(str(root), encoding="utf-8")
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)


def create(root, identifier, kind):
    if root.exists():
        raise ValueError(f"destination already exists: {root}")
    if not re.fullmatch(r"[a-z][a-z0-9_]*", identifier) or identifier == "minecraftc":
        raise ValueError("new package id must match [a-z][a-z0-9_]* and not minecraftc")
    root.mkdir(parents=True)
    write_lf(root / ".gitattributes", "# Preserve runtime resource bytes across platform checkouts.\n* -text\n")
    value = {"id": identifier, "name": identifier.replace("_", " ").title(),
             "version": "1.0.0", "api": 1, "package_format": 1}
    if kind == "native":
        value["native"] = {"windows": f"{identifier}.dll", "linux": f"{identifier}.so",
                           "macos": f"{identifier}.dylib"}
    write_json(root / "mod.json", value)
    for directory in CATEGORIES + RESOURCES:
        (root / directory).mkdir()
    key = identifier + ":blue_brick"
    write_json(root / "materials/blue_brick.json", {"key": key, "color": [0.2, 0.5, 0.9, 1]})
    write_json(root / "blocks/blue_brick.json", {"key": key, "name": "Blue Brick",
               "name_key": identifier + ":block.blue_brick", "hardness": 1.5,
               "materials": key, "drop": key})
    write_json(root / "items/blue_brick.json", {"key": key, "name": "Blue Brick",
               "name_key": identifier + ":block.blue_brick", "placed_block": key,
               "material": key, "category": 0})
    write_json(root / "items/crystal.json", {"key": identifier + ":crystal", "name": "Resonant Crystal",
               "name_key": identifier + ":item.crystal", "material": key})
    write_json(root / "recipes/blue_brick.json", {"key": key, "width": 1, "height": 1,
               "ingredients": ["minecraftc:cobblestone"], "output": key, "count": 4})
    write_json(root / "lang/en_us.json", {identifier + ":block.blue_brick": "Blue Brick", identifier + ":item.crystal": "Resonant Crystal"})
    write_json(root / "lang/zh_cn.json", {identifier + ":block.blue_brick": "蓝砖", identifier + ":item.crystal": "共鸣水晶"})
    write_lf(root / "README.md",
        f"# {value['name']}\n\nDefinitions and resource paths are relative to this package.\n"
        "Build with content_package.py build, then copy the output package to your mods directory.\n"
        "Restart after enabling. Use a new disposable world for development.\n"
        "Add your asset/code licenses in LICENSE.md and provenance next to the resources.\n")
    if kind == "native":
        (root / "code").mkdir()
        write_lf(root / "code/plugin.cpp", NATIVE_TEMPLATE.replace("PACKAGE_ID", identifier))
        write_lf(root / "CMakeLists.txt", CMAKE_TEMPLATE.replace("PACKAGE_ID", identifier))


NATIVE_TEMPLATE = '''#include <minecraftc/plugin.hpp>
namespace {
const MC_Host* host=nullptr;
uint16_t brick=MC_INVALID_ID;
uint16_t crystal=MC_INVALID_ID;
int32_t use(MC_PluginContext* context,MC_Event* event,void*) {
    if(event->item!=crystal)return 0;
    event->cancelled=1;
    return host->give_item(context,brick,1);
}
int32_t gift(MC_PluginContext* context,MC_Event*,void*) {
    return host->give_item(context,brick,4);
}
int32_t load(const MC_Host* api,MC_PluginContext* context) {
    host=api;
    const auto block=api->find_block(context,"PACKAGE_ID:blue_brick");
    brick=api->find_item(context,"PACKAGE_ID:blue_brick");
    crystal=api->find_item(context,"PACKAGE_ID:crystal");
    if(block==MC_INVALID_ID||brick==MC_INVALID_ID||crystal==MC_INVALID_ID)return -1;
    if(api->subscribe(context,MC_USE_PRE,0,use,nullptr))return -1;
    return api->register_command(context,"PACKAGE_ID:gift",gift,nullptr);
}
const MC_Plugin plugin{sizeof(MC_Plugin),MC_PLUGIN_ABI,"PACKAGE_ID","1.0.0",load,nullptr};
}
extern "C" MC_PLUGIN_EXPORT const MC_Plugin* MCPlugin_Query(uint32_t abi) {
    return abi==MC_PLUGIN_ABI?&plugin:nullptr;
}
'''
CMAKE_TEMPLATE = '''cmake_minimum_required(VERSION 3.16)
project(PACKAGE_ID LANGUAGES CXX C)
set(MINECRAFTC_SDK "" CACHE PATH "MinecraftC SDK directory")
include("${MINECRAFTC_SDK}/cmake/ContentPackage.cmake")
minecraftc_add_content_package(PACKAGE_ID ROOT "${CMAKE_CURRENT_SOURCE_DIR}" MODE MODULE)
'''


def build(root, output, config, sdk):
    value = manifest(root)
    destination = output / value["id"]
    if destination.exists():
        raise ValueError(f"output already exists: {destination}")
    if output == root or root in output.parents or output in root.parents:
        raise ValueError("output must be outside the source package")
    module = None
    if "native" in value:
        source_key = hashlib.sha256(str(root.resolve()).encode("utf-8")).hexdigest()[:12]
        cache = output.parent / ".content-package-build" / value["id"] / source_key
        subprocess.run(["cmake", "-S", str(root), "-B", str(cache),
                        f"-DMINECRAFTC_SDK={sdk}", f"-DPython3_EXECUTABLE={sys.executable}",
                        f"-DCMAKE_BUILD_TYPE={config}"], check=True)
        subprocess.run(["cmake", "--build", str(cache), "--config", config,
                        "--target", "content_package_module"], check=True)
        module = (cache / f"content-module-{config}.txt").read_text(encoding="utf-8").strip()
    stage(root, destination, module=module)
    print(destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True, metavar="{create,build}")
    command = sub.add_parser("create", help="Create a new source package")
    command.add_argument("directory", type=Path)
    command.add_argument("--id", required=True)
    command.add_argument("--type", choices=("data", "native"), default="data")
    command = sub.add_parser("build", help="Build and publish a clean runtime directory")
    command.add_argument("directory", type=Path)
    command.add_argument("--output", type=Path, required=True)
    command.add_argument("--config", default="Release")
    command.add_argument("--sdk", type=Path, default=SDK_DEFAULT)
    # Build-system commands also share exactly the same runtime file selection.
    command = sub.add_parser("stage")
    command.add_argument("directory", type=Path)
    command.add_argument("--output", type=Path, required=True)
    command.add_argument("--refresh", action="store_true")
    command = sub.add_parser("metadata")
    command.add_argument("directory", type=Path)
    args = parser.parse_args()
    try:
        root = args.directory.absolute()
        if args.command == "create":
            create(root, args.id, args.type)
        elif args.command == "metadata":
            value = manifest(root)
            print(value["id"])
        elif args.command == "stage":
            stage(root, args.output.absolute(), builtin=True, refresh=args.refresh)
        else:
            build(root, args.output.absolute(), args.config, args.sdk.absolute())
    except (ValueError, OSError, subprocess.CalledProcessError, KeyError) as error:
        parser.exit(1, f"content package: {error}\n")


if __name__ == "__main__":
    main()
