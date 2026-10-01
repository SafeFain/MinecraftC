#!/usr/bin/env python3
"""Validate local task records or retain a stage checkpoint without Git writes."""

from __future__ import annotations

import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys


CONTEXT = ("AGENTS.md", "CLAUDE.md", "PLAN.md", "PROGRESS.md", "TASKS.md")
LIMITS = {"AGENTS.md": 300, "PLAN.md": 250, "PROGRESS.md": 150}
FIELDS = {
    "status": r"状态|Status",
    "start date": r"开始日期|Start date",
    "completion date": r"完成日期|Completion date",
    "commits": r"相关提交|Related commits?",
    "branch": r"相关分支|Related branch",
}


def check(root: Path) -> list[str]:
    errors = []
    texts = {}
    for name in CONTEXT:
        path = root / name
        if not path.is_file():
            errors.append(f"missing local context: {name}")
            continue
        texts[name] = path.read_text(encoding="utf-8")
        if name in LIMITS and len(texts[name].splitlines()) > LIMITS[name]:
            errors.append(f"{name}: exceeds {LIMITS[name]} lines")
    if len(texts) != len(CONTEXT):
        return errors

    active = re.search(r"^## Active Task\s*\n(.*?)(?=^## |\Z)",
                       texts["TASKS.md"], re.M | re.S)
    if not active:
        errors.append("TASKS.md: missing Active Task section")
    else:
        idle = bool(re.match(r"(?:无[。\s]|None\b)", active.group(1).strip(), re.I))
        for name in ("PLAN.md", "PROGRESS.md"):
            is_idle = "当前没有活动任务。" in texts[name]
            if idle != is_idle:
                errors.append(f"{name}: disagrees with TASKS.md activity state")
        if not idle:
            titles = [re.search(r"^# Task (?:Plan|Progress):\s*(.+)$", texts[name], re.M)
                      for name in ("PLAN.md", "PROGRESS.md")]
            if not all(titles) or titles[0].group(1) != titles[1].group(1):
                errors.append("PLAN.md / PROGRESS.md: active task titles differ")
            elif titles[0].group(1) not in active.group(1):
                errors.append("TASKS.md: active task title differs")

    references = []
    for line in texts["TASKS.md"].splitlines():
        if not re.match(r"^\|\s*\d{4}-\d{2}-\d{2}\s*\|", line):
            continue
        match = re.search(r"docs/tasks/archive/[^`|\s]+\.md", line)
        if not match:
            errors.append(f"TASKS.md: archive missing in row: {line}")
        else:
            references.append(match.group())
    for name, count in Counter(references).items():
        if count > 1:
            errors.append(f"duplicate archive index: {name}")
    archives = {p.relative_to(root).as_posix(): p
                for p in (root / "docs/tasks/archive").glob("*.md")}
    for name in sorted(set(references) - archives.keys()):
        errors.append(f"index points to missing archive: {name}")
    for name in sorted(archives.keys() - set(references)):
        errors.append(f"archive missing from index: {name}")
    for name, path in sorted(archives.items()):
        if not re.fullmatch(r"\d{4}-\d{2}-\d{2}-.+\.md", path.name):
            errors.append(f"invalid archive name: {name}")
        text = path.read_text(encoding="utf-8")
        basic = re.search(r"^## (?:基本信息|Basic Information)\s*\n(.*?)(?=^## |\Z)",
                          text, re.M | re.S)
        if not basic:
            errors.append(f"archive missing basic information: {name}")
            continue
        for field, aliases in FIELDS.items():
            pattern = rf"^- (?:{aliases})\s*[:：]\s*\S.*$"
            if not re.search(pattern, basic.group(1), re.M | re.I):
                errors.append(f"archive missing {field}: {name}")
    return errors


def git_output(root: Path, *args: str) -> bytes:
    return subprocess.check_output(["git", *args], cwd=root)


def checkpoint(root: Path, task: str, stage: str, authorization: str,
               logs: list[Path], files: list[Path]) -> Path:
    for value in (task, stage):
        if not re.fullmatch(r"[a-z0-9][a-z0-9_-]*", value):
            raise ValueError("task and stage must use lowercase letters, digits, hyphens or underscores")
    now = datetime.now(timezone.utc)
    target = root / "docs/tasks/evidence" / task / f"{now:%Y%m%dT%H%M%S%fZ}-{stage}"
    # Resolve all inputs and Git evidence before creating the checkpoint.
    sources = [(name, root / name) for name in CONTEXT]
    for path in files:
        source = (root / path).resolve()
        relative = source.relative_to(root)
        sources.append((f"files/{relative.as_posix()}", source))
    sources += [(f"logs/{i}-{path.name}", path.resolve()) for i, path in enumerate(logs)]
    contents = {name: path.read_bytes() for name, path in sources}
    contents["git-status.txt"] = git_output(root, "status", "--short")
    contents["git-diff.patch"] = git_output(root, "diff", "--binary")
    contents["git-staged-diff.patch"] = git_output(root, "diff", "--cached", "--binary")
    manifest = {
        "utc": now.isoformat(), "task": task, "stage": stage,
        "head": git_output(root, "rev-parse", "HEAD").decode().strip(),
        "branch": git_output(root, "branch", "--show-current").decode().strip(),
        "authorization_quote_supplied_by_caller": authorization or "未记录",
        "note": "A checkpoint records present evidence; it does not prove earlier actions or validate caller claims.",
        "sha256": {name: hashlib.sha256(data).hexdigest() for name, data in contents.items()},
    }
    target.mkdir(parents=True, exist_ok=False)
    for name, data in contents.items():
        path = target / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    (target / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
                                          encoding="utf-8")
    return target


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("check")
    save = commands.add_parser("checkpoint")
    save.add_argument("--task", required=True)
    save.add_argument("--stage", required=True)
    save.add_argument("--authorization", default="")
    save.add_argument("--log", action="append", type=Path, default=[])
    save.add_argument("--file", action="append", type=Path, default=[],
                      help="include an existing workspace file, e.g. an untracked validation scene")
    args = parser.parse_args()
    root = args.root.resolve()
    try:
        if args.command == "checkpoint":
            print(checkpoint(root, args.task, args.stage, args.authorization, args.log, args.file))
            return 0
        errors = check(root)
        if errors:
            print("Task-state check failed:\n" + "\n".join(f"- {e}" for e in errors), file=sys.stderr)
            return 1
        count = len(list((root / "docs/tasks/archive").glob("*.md")))
        print(f"Task-state check passed: {count} archives; activity, index, metadata and length limits agree")
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Task-state operation failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
