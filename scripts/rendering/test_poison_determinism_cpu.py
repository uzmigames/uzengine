"""CPU-only regression tests for allocation-poison verdicts; never captures."""
import copy
import unittest
from unittest.mock import patch

import numpy as np

from test_poison_determinism import PATTERNS, capture_run, compare_case


class PoisonVerdictTests(unittest.TestCase):
    def setUp(self):
        self.entries = [
            {'dir': str(i), 'pattern': PATTERNS[i // 2], 'preload_frames': 42,
             'exit_code': 0, 'poison_active': True}
            for i in range(4)
        ]
        self.images = [np.zeros((2, 2, 3), dtype=np.int16) for _ in self.entries]

    def test_identical_pairs_pass(self):
        self.assertEqual(compare_case(self.entries, self.images)['verdict'], 'PASS')
        self.assertEqual(len(compare_case(self.entries, self.images)['pairs']), 6)

    def test_across_pattern_difference_fails(self):
        images = self.images[:2] + [image + 1 for image in self.images[2:]]
        self.assertEqual(compare_case(self.entries, images)['verdict'], 'FAIL')

    def test_within_pattern_difference_is_indeterminate(self):
        images = [self.images[0] + 1] + self.images[1:]
        self.assertEqual(compare_case(self.entries, images)['verdict'], 'INDETERMINATE')

    def test_different_preloads_are_unmatched(self):
        for i, entry in enumerate(self.entries):
            entry['preload_frames'] += i
        self.assertEqual(compare_case(self.entries, self.images)['verdict'], 'UNMATCHED')

    def test_unknown_preloads_do_not_match(self):
        for entry in self.entries:
            entry['preload_frames'] = None
        self.assertEqual(compare_case(self.entries, self.images)['verdict'], 'UNMATCHED')

    def test_cross_pair_without_repeat_control_is_unmatched(self):
        self.entries[1]['preload_frames'] = 43
        self.assertEqual(compare_case(self.entries, self.images)['verdict'], 'UNMATCHED')

    def test_missing_activation_or_frame_fails(self):
        entries = copy.deepcopy(self.entries)
        entries[0]['poison_active'] = False
        self.assertEqual(compare_case(entries, self.images)['verdict'], 'FAIL')
        self.assertEqual(compare_case(self.entries, [None] + self.images[1:])['verdict'], 'FAIL')

    def test_capture_passes_pattern_to_shared_runner(self):
        with patch('test_poison_determinism.runner.run_capture') as run_capture:
            run_capture.return_value = {'exit_code': 0, 'manifest': None}
            with patch('test_poison_determinism.os.path.exists', return_value=False):
                capture_run('out', 'case', 'project', 120, {}, '0xBF', 1)
            self.assertEqual(run_capture.call_args.kwargs['extra_args'],
                             ('--poison-allocations', '0xBF'))
            self.assertEqual(run_capture.call_args.kwargs['screenshot_frames'], '120')


if __name__ == '__main__':
    unittest.main()
