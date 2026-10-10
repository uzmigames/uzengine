"""Render the project priority overlay without changing native Rulebook tasks.

Run from any directory: python <repo>/scripts/tools/rulebook_queue.py [--check]
"""

from __future__ import annotations

import argparse
import json
import os
from collections import Counter
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
CHECK = re.compile(r"^\s*- \[([ xX])\]\s+(.*)$", re.MULTILINE)
ITEM = re.compile(r"^(\d+(?:\.\d+)*)\s")
PRIORITIES = ("P0", "P1", "P2", "P3")


def read_task(path: Path, archived: bool = False) -> dict:
    metadata = path / ".metadata.json"
    meta = json.loads(metadata.read_text(encoding="utf-8-sig")) if metadata.exists() else {}
    checklist = path / "tasks.md"
    content = checklist.read_text(encoding="utf-8-sig") if checklist.exists() else ""
    rows = [(mark.lower() == "x", text) for mark, text in CHECK.findall(content)]
    items = {}
    for done, text in rows:
        match = ITEM.match(text)
        if match:
            # Legacy tasks can repeat numbering; a selector must never hide that.
            key = match[1]
            items[key] = None if key in items else (done, text)
    questions = [q for q in meta.get("questions", []) if q.get("status", "open") == "open"]
    return {"path": path, "meta": meta, "status": meta.get("status", "untracked"),
            "rows": rows, "items": items, "done": sum(done for done, _ in rows),
            "total": len(rows), "archived": archived, "questions": questions}


def is_complete(task: dict) -> bool:
    return bool((task["status"] == "completed" or task["archived"])
                and task["total"] and task["done"] == task["total"]
                and not task["questions"])


def selected(task: dict, ids: list[str]) -> list[tuple[bool, str]]:
    result = []
    for item in ids:
        value = task["items"].get(item)
        if value is None:
            raise ValueError(f"Missing or ambiguous checklist item {task['path'].name}:{item}")
        result.append(value)
    return result


def load_plan(root: Path) -> tuple[dict, dict, dict]:
    config = json.loads((root / ".rulebook/task-priorities.json").read_text(encoding="utf-8"))
    active = {p.name: read_task(p) for p in sorted((root / ".rulebook/tasks").iterdir()) if p.is_dir()}
    all_tasks = dict(active)
    archive = root / ".rulebook/archive"
    if archive.exists():
        for path in sorted(archive.iterdir(), reverse=True):
            if path.is_dir():
                task_id = re.sub(r"^\d{4}-\d{2}-\d{2}-", "", path.name)
                if task_id not in all_tasks:
                    all_tasks[task_id] = read_task(path, archived=True)
    validate(config, active, all_tasks)
    return config, active, all_tasks


def validate(config: dict, active: dict, tasks: dict) -> None:
    entries = config["tasks"]
    ids = [e["id"] for e in entries]
    if len(ids) != len(set(ids)):
        raise ValueError("Duplicate task classification")
    missing = [i for i, t in active.items() if i not in ids and t["status"] != "completed"]
    if missing:
        raise ValueError("Unclassified tasks: " + ", ".join(missing))
    for entry in entries:
        if entry["id"] not in tasks:
            raise ValueError(f"Unknown task: {entry['id']}")
        if entry.get("lane", "execute") not in ("execute", "review", "coordination"):
            raise ValueError(f"Invalid lane: {entry['id']}")
    milestones = config.get("milestones", [])
    keys = [m["key"] for m in milestones]
    if len(keys) != len(set(keys)):
        raise ValueError("Duplicate milestone key")
    for m in milestones:
        if m["task"] not in tasks:
            raise ValueError(f"Unknown milestone task: {m['task']}")
        selected(tasks[m["task"]], m["items"])
    nodes = {"task:" + e["id"]: e for e in entries if e.get("lane", "execute") == "execute"}
    nodes.update({"milestone:" + m["key"]: m for m in milestones})
    gates = config.get("gates", {})
    gate_owner = {}
    for key, gate in gates.items():
        if gate["task"] not in tasks:
            raise ValueError(f"Unknown gate task: {gate['task']}")
        selected(tasks[gate["task"]], gate["items"])
        owners = [m for m in milestones if m["task"] == gate["task"] and set(gate["items"]) <= set(m["items"])]
        if len(owners) != 1:
            raise ValueError(f"Gate {key} needs one milestone owner")
        gate_owner[key] = "milestone:" + owners[0]["key"]
    edges = {}
    for key, entry in nodes.items():
        if entry["priority"] not in PRIORITIES or not isinstance(entry["order"], int):
            raise ValueError(f"Invalid priority/order: {key}")
        dependencies = []
        for dep in entry.get("depends_on", []):
            if dep not in tasks:
                raise ValueError(f"Unknown dependency: {dep}")
            if "task:" + dep in nodes:
                dependencies.append("task:" + dep)
        for gate in entry.get("gates", []):
            if gate not in gates:
                raise ValueError(f"Unknown gate: {gate}")
            dependencies.append(gate_owner[gate])
        edges[key] = dependencies
    visited, stack = set(), set()

    def visit(key: str) -> None:
        if key in stack:
            raise ValueError(f"Dependency cycle at {key}")
        if key in visited:
            return
        stack.add(key)
        for dep in edges[key]:
            visit(dep)
        stack.remove(key)
        visited.add(key)

    for key in nodes:
        visit(key)
    for key, deps in edges.items():
        for dep in deps:
            if sort_key(nodes[dep]) >= sort_key(nodes[key]):
                raise ValueError(f"Dependency ordered after its consumer: {dep} -> {key}")


def sort_key(entry: dict) -> tuple:
    return (PRIORITIES.index(entry["priority"]), entry["order"])


def blockers(entry: dict, config: dict, tasks: dict) -> list[str]:
    task = tasks[entry.get("task", entry.get("id"))]
    reasons = []
    if task["status"] == "blocked":
        reasons.append("existing Rulebook blocker: read task evidence")
    if task["questions"]:
        reasons.append("open operator question")
    for dep in entry.get("depends_on", []):
        if not is_complete(tasks[dep]):
            reasons.append(dep)
    for name in entry.get("gates", []):
        gate = config["gates"][name]
        if not all(done for done, _ in selected(tasks[gate["task"]], gate["items"])):
            reasons.append("gate " + name)
    return reasons


def work_items(config: dict, active: dict, tasks: dict) -> list[dict]:
    work = []
    for entry in config["tasks"]:
        if (entry["id"] in active and entry.get("lane", "execute") == "execute"
                and tasks[entry["id"]]["status"] != "completed"):
            work.append(entry)
    for milestone in config.get("milestones", []):
        if not all(done for done, _ in selected(tasks[milestone["task"]], milestone["items"])):
            work.append(milestone)
    return sorted(work, key=sort_key)


def title(task_id: str) -> str:
    value = re.sub(r"^phase\d+[a-z]?[-_]", "", task_id)
    value = re.sub(r"^\d+(?:\.\d+)+[-_]", "", value)
    return value.replace("_", " ").replace("-", " ").capitalize()


def cell(value: str) -> str:
    return re.sub(r"\s+", " ", value).replace("|", "\\|")


def render(root: Path, destination: Path, config: dict, active: dict, tasks: dict) -> str:
    def link(path: Path, label: str) -> str:
        return f"[{cell(label)}]({Path(os.path.relpath(path, destination.parent)).as_posix()})"

    def task_link(task_id: str, label: str | None = None) -> str:
        folder = tasks[task_id]["path"]
        path = folder / "tasks.md" if (folder / "tasks.md").exists() else folder
        return link(path, label or title(task_id))

    work = work_items(config, active, tasks)
    classified = {e["id"]: e for e in config["tasks"]}
    counts = Counter(t["status"] for t in active.values())
    lines = ["# Rulebook execution priorities", "",
             "> Generated by `python scripts/tools/rulebook_queue.py`; do not edit this view.", "",
             f"Priority review: {config['reviewed']}. {config['objective']}", "",
             "**Start with the first ready item below. Phase numbers and old `in-progress` metadata do not set priority.**", "",
             f"Inventory: {len(active)} task directories; " + "; ".join(f"{n} {s}" for s, n in sorted(counts.items())) + ".",
             "Progress is read from current checklists. Completed metadata is a closure claim, not fresh test evidence.", "",
             "P0 = trustworthy execution and state/lifetime blockers; P1 = current visual correctness; P2 = complete existing paths and measured improvements; P3 = expansion.", "",
             "Planning gates and dependencies below are local queue constraints; they do not rewrite native Rulebook status.", ""]
    ready = [e for e in work if not blockers(e, config, tasks)]
    if ready:
        e = ready[0]
        tid = e.get("task", e.get("id"))
        rows = selected(tasks[tid], e["items"]) if "items" in e else tasks[tid]["rows"]
        next_text = next((text for done, text in rows if not done), "Review the task's completion evidence.")
        lines += ["## Next action", "", task_link(tid, e.get("title", title(tid))), "", cell(next_text), ""]
    else:
        lines += ["## Next action", "", "No execution item is ready. Resolve the listed dependencies or explicit blockers; do not auto-start a review-only task.", ""]
    for priority in PRIORITIES:
        entries = [e for e in work if e["priority"] == priority]
        lines += [f"## {priority} — execution order ({len(entries)} items)", "",
                  "| Order | Work and scope | Why now | Readiness |", "|---|---|---|---|"]
        for e in entries:
            tid = e.get("task", e.get("id"))
            task = tasks[tid]
            scope = "items " + ", ".join(e["items"]) if "items" in e else f"{task['done']}/{task['total']} checked; {task['status']}"
            waiting = blockers(e, config, tasks)
            readiness = "Ready for next action" if not waiting else "Waiting: " + "; ".join(waiting)
            lines.append(f"| {e['order']} | {task_link(tid, e.get('title', title(tid)))} — {scope} | {cell(e['reason'])} | {cell(readiness)} |")
        lines.append("")
    reviews = []
    coordination = []
    history = []
    for tid, task in active.items():
        entry = classified.get(tid, {})
        if entry.get("lane") == "review" or (task["status"] == "completed" and not is_complete(task)):
            reviews.append((tid, entry))
        elif task["status"] == "completed":
            history.append(tid)
        elif entry.get("lane") == "coordination":
            coordination.append((tid, entry))
    lines += [f"## Reconcile before scheduling or archiving ({len(reviews)})", "",
              "These are scope/status reviews, not new implementation assignments. Do not repeat old implementations or mark remaining work done just to archive.", "",
              "| Task | Metadata / checklist | Required reconciliation |", "|---|---|---|"]
    for tid, entry in sorted(reviews, key=lambda row: row[1].get("order", 9999)):
        t = tasks[tid]
        reason = entry.get("reason", "Completed status has unresolved checklist or decision evidence; review closure.")
        lines.append(f"| {task_link(tid)} | {t['status']}; {t['done']}/{t['total']} | {cell(reason)} |")
    lines += ["", "## Coordination — do not execute as a competing feature task", ""]
    for tid, entry in sorted(coordination, key=lambda row: row[1]["order"]):
        lines.append(f"- {task_link(tid)}: {cell(entry['reason'])}")
    lines += ["", f"## Completed-status closure candidates ({len(history)})", "",
              "The user asked Claude to archive completed tasks. " + link(root / "docs/planning/claude-rulebook-archive-handoff.md", "Read the archive handoff") + ".",
              "Validate evidence and archive with Rulebook; this view does not archive or certify implementations.", "",
              "<details>", "<summary>Show candidates outside the execution queue</summary>", ""]
    for tid in sorted(history):
        lines.append(f"- {task_link(tid)} (`{tid}`)")
    lines += ["", "</details>", "", "## Maintenance", "",
              "- Edit " + link(root / ".rulebook/task-priorities.json", "the priority manifest") + ", then run `python scripts/tools/rulebook_queue.py`.",
              "- Run `python scripts/tools/rulebook_queue.py --check` to detect stale views, missing classification or invalid dependency order.",
              "- Refresh after native Rulebook creates, status updates or archives; its stock index generator still sorts by phase.",
              "- The stable entry point is " + link(root / ".rulebook/PRIORITIES.md", ".rulebook/PRIORITIES.md") + "; the task-root README is regenerated with the same priority view.",
              "- New tasks must be classified; archived task references remain resolvable. IDs and historical checklists are preserved.",
              "- A dependency is satisfied only by completed/archived metadata with a nonempty, fully checked checklist and no open question. Superseded work is not silently treated as implemented.", ""]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="validate without writing; fail if generated views differ")
    args = parser.parse_args()
    try:
        config, active, tasks = load_plan(ROOT)
        stale = []
        for destination in (ROOT / ".rulebook/PRIORITIES.md", ROOT / ".rulebook/tasks/README.md"):
            content = render(ROOT, destination, config, active, tasks)
            previous = destination.read_text(encoding="utf-8") if destination.exists() else ""
            if previous != content:
                stale.append(destination.relative_to(ROOT).as_posix())
                if not args.check:
                    destination.write_text(content, encoding="utf-8", newline="\n")
        if args.check and stale:
            raise ValueError("Stale priority views: " + ", ".join(stale))
        print(f"Queue valid: {len(active)} task directories; {len(work_items(config, active, tasks))} execution items; {len(stale)} views {'stale' if args.check else 'updated'}.")
        return 0
    except (ValueError, KeyError, OSError) as exc:
        print(f"Queue error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
