#!/bin/sh
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if test -x "$script_dir/MinecraftC.app/Contents/MacOS/MinecraftC"; then
    exec "$script_dir/MinecraftC.app/Contents/MacOS/MinecraftC" "$@"
fi
exec "$script_dir/minecraftc" "$@"
