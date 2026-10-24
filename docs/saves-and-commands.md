# Saves and commands

## Data locations

| Platform | Default save directory |
|---|---|
| Windows | `%APPDATA%\MinecraftC\saves` |
| macOS | `~/Library/Application Support/MinecraftC/saves` |
| Linux | `$XDG_DATA_HOME/minecraftc/saves`, or `~/.local/share/minecraftc/saves` |
| Android / iOS | Application-private preference directory |

On desktop, an existing `saves/` in the launch directory takes precedence and
that launch directory becomes the user-data root. Otherwise, user data is the
parent of the default save directory above. Settings, logs, `mods/`, `plugins.json`
and `config/plugins/` use this root. Mobile paths come from SDL preferences.
Assets are read-only and resolved separately from the executable/package.

## Compatibility and recovery

Worlds require **save v22**, **global generation v21**, and matching gameplay
plugin requirements. The optional official Heaven provider uses generation **v9**.
Save-format and generation versions are independent of the application version.
Older save metadata (v2–v21) remains inspectable in the world list, but opening
unsupported formats or generation versions is refused without rewriting files.

A missing, extra or changed gameplay plugin can trigger a local compatibility
warning. Cancel is the default. Review the listed versions, fingerprints, content,
dimensions and generation selections before choosing recovery:

- **Back Up and Enter Anyway** converts a staged copy and retains the complete
  original at the backup location shown after completion.
- **Enter Without Backup** uses the same conversion, then discards the original
  after publication succeeds. Removed content cannot be restored by this option.

Missing blocks become air; unavailable items/entities and dimensions are removed.
A missing active dimension returns the player to the Overworld. Available container
contents are preserved; derived caches rebuild. Plugin-private files and LAN
profiles keep their own compatibility rules. Recovery cannot bypass corrupt data
or unsupported save/generation versions. See [full recovery rules](plugins.md#saves-and-limits).

LAN requires matching gameplay plugins; visual-only packages may differ.
The wire protocol is v6 and guest profiles are v4. Older profiles are refused.
See [LAN play](lan-multiplayer.md).

## Commands

T opens chat; `/` opens a command. Tab/Shift+Tab cycle completions.
Commands that change gameplay require cheats; administrative LAN commands are
host-only. `/help` lists commands and `/help gamerule [<rule>]` describes rules.

| Command | Purpose |
|---|---|
| `/gamemode` | Change game mode |
| `/tp` | Teleport |
| `/time`, `/weather` | Change time or weather |
| `/give <item_name> [1..64]` | Give an item |
| `/gamerule <rule> [<value>]` | Query or set a world rule |
| `/locate biome <biome>` | Find a biome |
| `/locate structure <structure>` | Find a structure in the active dimension |
| `/dimension list`, `/dimension <key>` | List or travel to registered dimensions |

The day/night cycle defaults to 1200 seconds. Set it per world with
`/gamerule DayNightDuration 1200`. Registered dimensions may override time policies.
See [game rules and limitations](game-rules.md), [structure names](project-features.md#overworld-structures-generation-v18)
and [plugin commands](plugins.md#lan-multiplayer-and-abi-1-extension).
