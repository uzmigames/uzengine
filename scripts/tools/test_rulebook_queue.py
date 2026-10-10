"""Behavioral checks for the repository-owned Rulebook planning overlay."""

import copy
import json
from pathlib import Path
import tempfile
import unittest

import rulebook_queue as queue


class PriorityQueueTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="uzengine-rulebook-queue-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / ".rulebook/tasks").mkdir(parents=True)
        self.config = {"version": 1, "reviewed": "2026-10-10", "objective": "Test ordering.",
                       "tasks": [], "gates": {}, "milestones": []}

    def task(self, task_id, status="pending", checklist="- [ ] 1.1 Validate behavior\n", archived=False):
        base = self.root / ".rulebook" / ("archive" if archived else "tasks")
        directory = base / (("2026-10-10-" if archived else "") + task_id)
        directory.mkdir(parents=True)
        (directory / ".metadata.json").write_text(json.dumps({"status": status}), encoding="utf-8")
        (directory / "tasks.md").write_text(checklist, encoding="utf-8")
        return directory

    def entry(self, task_id, order, **extra):
        entry = {"id": task_id, "priority": "P0", "order": order, "reason": "A concrete test reason."}
        entry.update(extra)
        self.config["tasks"].append(entry)
        return entry

    def load(self):
        (self.root / ".rulebook/task-priorities.json").write_text(json.dumps(self.config), encoding="utf-8")
        return queue.load_plan(self.root)

    def test_priority_beats_phase_and_closed_work_leaves_execution(self):
        self.task("phase9_critical")
        self.task("phase0_later")
        self.task("phase0_closed", "completed", "- [x] 1.1 Verified\n")
        self.entry("phase9_critical", 10)
        self.entry("phase0_later", 20)
        config, active, tasks = self.load()
        self.assertEqual([e["id"] for e in queue.work_items(config, active, tasks)],
                         ["phase9_critical", "phase0_later"])
        text = queue.render(self.root, self.root / ".rulebook/PRIORITIES.md", config, active, tasks)
        self.assertIn("Completed-status closure candidates (1)", text)

    def test_unclassified_new_task_is_not_silently_low_priority(self):
        self.task("phase0_new")
        with self.assertRaisesRegex(ValueError, "Unclassified"):
            self.load()

    def test_missing_dependency_and_reversed_order_fail(self):
        self.task("phase0_a")
        self.task("phase0_b")
        first = self.entry("phase0_a", 10, depends_on=["phase0_missing"])
        self.entry("phase0_b", 20)
        with self.assertRaisesRegex(ValueError, "Unknown dependency"):
            self.load()
        first["depends_on"] = ["phase0_b"]
        with self.assertRaisesRegex(ValueError, "ordered after"):
            self.load()

    def test_dependency_cycle_fails(self):
        self.task("phase0_a")
        self.task("phase0_b")
        self.entry("phase0_a", 10, depends_on=["phase0_b"])
        self.entry("phase0_b", 20, depends_on=["phase0_a"])
        with self.assertRaisesRegex(ValueError, "cycle"):
            self.load()

    def test_archived_predecessor_resolves_but_unfinished_archive_does_not_pass(self):
        path = self.task("phase0_a", "completed", "- [x] 1.1 Verified\n", archived=True)
        self.task("phase0_b")
        self.entry("phase0_a", 10)
        successor = self.entry("phase0_b", 20, depends_on=["phase0_a"])
        config, active, tasks = self.load()
        self.assertNotIn("phase0_a", active)
        self.assertEqual(queue.blockers(successor, config, tasks), [])
        (path / "tasks.md").write_text("- [ ] 1.1 Unfinished scope\n", encoding="utf-8")
        config, _, tasks = self.load()
        self.assertEqual(queue.blockers(successor, config, tasks), ["phase0_a"])

    def test_gate_observes_selected_items_not_whole_umbrella(self):
        path = self.task("phase0_umbrella", checklist="- [ ] 1.1 Baseline\n- [ ] 2.1 Later\n")
        self.task("phase0_consumer")
        self.entry("phase0_umbrella", 1, lane="coordination")
        consumer = self.entry("phase0_consumer", 20, gates=["baseline"])
        self.config["gates"]["baseline"] = {"task": "phase0_umbrella", "items": ["1.1"]}
        self.config["milestones"] = [{"key": "baseline", "task": "phase0_umbrella", "items": ["1.1"],
                                      "priority": "P0", "order": 10, "reason": "Baseline."}]
        config, _, tasks = self.load()
        self.assertEqual(queue.blockers(consumer, config, tasks), ["gate baseline"])
        (path / "tasks.md").write_text("- [x] 1.1 Baseline\n- [ ] 2.1 Later\n", encoding="utf-8")
        config, active, tasks = self.load()
        self.assertEqual(queue.blockers(consumer, config, tasks), [])
        self.assertFalse(queue.is_complete(tasks["phase0_umbrella"]))
        self.assertEqual(len(queue.work_items(config, active, tasks)), 1)

    def test_completed_with_open_work_and_review_only_are_not_ready_assignments(self):
        self.task("phase0_claimed", "completed", "- [ ] 1.1 Missing evidence\n")
        self.task("phase0_review")
        self.entry("phase0_review", 10, lane="review")
        config, active, tasks = self.load()
        self.assertEqual(queue.work_items(config, active, tasks), [])
        text = queue.render(self.root, self.root / ".rulebook/PRIORITIES.md", config, active, tasks)
        self.assertIn("Reconcile before scheduling or archiving (2)", text)
        self.assertIn("No execution item is ready", text)

    def test_missing_or_ambiguous_milestone_item_fails(self):
        path = self.task("phase0_a")
        self.entry("phase0_a", 1, lane="coordination")
        self.config["milestones"] = [{"key": "a", "task": "phase0_a", "items": ["9.9"],
                                      "priority": "P0", "order": 10, "reason": "Test."}]
        with self.assertRaisesRegex(ValueError, "Missing or ambiguous"):
            self.load()
        self.config["milestones"][0]["items"] = ["1.1"]
        (path / "tasks.md").write_text("- [x] 1.1 First\n- [ ] 1.1 Second\n", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "Missing or ambiguous"):
            self.load()

    def test_duplicate_classification_fails_and_render_is_deterministic(self):
        self.task("phase0_a")
        entry = self.entry("phase0_a", 10)
        self.config["tasks"].append(copy.deepcopy(entry))
        with self.assertRaisesRegex(ValueError, "Duplicate task"):
            self.load()
        self.config["tasks"].pop()
        config, active, tasks = self.load()
        destination = self.root / ".rulebook/PRIORITIES.md"
        self.assertEqual(queue.render(self.root, destination, config, active, tasks),
                         queue.render(self.root, destination, config, active, tasks))


if __name__ == "__main__":
    unittest.main()
