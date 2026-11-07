"""Offline source-fingerprint comparison tests; no CI or external evidence needed."""
import copy
import tempfile
import unittest
from unittest.mock import patch

from analyze import Analyzer

REF = '9' * 40
CANDIDATE = 'a' * 40


def bundle(commit):
    fingerprint = dict(frozenCommit=commit,
                       sourceEvaluatorConfigurationFiles={'engine/src/chess_core.cpp': {'gitBlob': 'b' * 40}},
                       windowsBuildArtifacts={'build/native-release/chess.exe': {'sha256': 'c' * 64, 'bytes': 123}},
                       linuxBuildArtifacts={'build/native-release/chess': {'sha256': 'd' * 64, 'bytes': 456}})
    platforms = []
    for platform in ('windows', 'linux'):
        platforms.append(dict(platform=platform, host={'os': platform}, toolchains={'node': 'v24.10.0'},
                              harness={'version': 'test'}, steps=[], sourceBefore=dict(
                                  trackedFingerprint='e' * 64, indexFingerprint='f' * 64,
                                  untrackedFingerprint='0' * 64)))
    return dict(fingerprints=fingerprint, platforms=platforms, invocations=[],
                deterministic={('linux', 'wasm-debug'): dict(
                    records=['original output'], recordCount=1, normalizedSha256='g' * 64)})


class CrossRevisionComparison(unittest.TestCase):
    def setUp(self):
        self.analyzer = Analyzer('.', CANDIDATE, '1' * 40)
        self.reference = bundle(REF)
        self.candidate = bundle(CANDIDATE)
        self.git_mock = patch('analyze.git', return_value='AGENTS.md\0scripts/validation/workflow.py\0').start()
        self.addCleanup(patch.stopall)

    def change_working_fingerprints(self):
        for p in self.candidate['platforms']:
            p['sourceBefore']['trackedFingerprint'] = '2' * 64
            p['sourceBefore']['indexFingerprint'] = '3' * 64

    def test_distinct_commits_allow_whole_tree_fingerprint_changes(self):
        self.change_working_fingerprints()
        self.analyzer.compare(self.candidate, self.reference)
        self.assertEqual(self.analyzer.issues, [])
        records = [row for row in self.analyzer.comparisons if row.get('scope') == 'source working fingerprints']
        self.assertEqual(len(records), 2)
        self.assertEqual({v['platform'] for v in records}, {'windows', 'linux'})
        self.assertTrue(all(v['comparison'] == 'not_comparable_across_commits' for v in records))

    def test_same_commit_still_requires_exact_source_fingerprints(self):
        self.candidate['fingerprints']['frozenCommit'] = REF
        self.change_working_fingerprints()
        self.analyzer.compare(self.candidate, self.reference)
        codes = [x['code'] for x in self.analyzer.issues]
        self.assertEqual(codes.count('source_working_fingerprint'), 4)

    def test_same_commit_untracked_fingerprint_still_compared(self):
        self.candidate['fingerprints']['frozenCommit'] = REF
        self.candidate['platforms'][0]['sourceBefore']['untrackedFingerprint'] = '9' * 64
        self.analyzer.compare(self.candidate, self.reference)
        self.assertEqual([x['path'] for x in self.analyzer.issues], ['windows/untrackedFingerprint'])

    def test_same_commit_matching_fingerprints_pass(self):
        self.candidate['fingerprints']['frozenCommit'] = REF
        self.analyzer.compare(self.candidate, self.reference)
        self.assertEqual(self.analyzer.issues, [])

    def test_changed_deterministic_records_still_fail_across_commits(self):
        self.change_working_fingerprints()
        self.candidate['deterministic'][('linux', 'wasm-debug')]['records'] = ['changed output']
        self.analyzer.compare(self.candidate, self.reference)
        self.assertEqual([x['code'] for x in self.analyzer.issues], ['deterministic_records'])

    def test_changed_evaluator_configuration_still_fails_across_commits(self):
        self.candidate['fingerprints']['sourceEvaluatorConfigurationFiles'][
            'engine/src/chess_core.cpp']['gitBlob'] = '8' * 40
        self.analyzer.compare(self.candidate, self.reference)
        self.assertIn('source_evaluator_configuration', [x['code'] for x in self.analyzer.issues])

    def test_engine_change_is_review_required_across_commits(self):
        self.git_mock.return_value = 'docs/CURRENT_STATUS.md\0engine/src/search/search.cpp\0'
        self.analyzer.compare(self.candidate, self.reference)
        self.assertIn('source_revision_requires_review', [x['code'] for x in self.analyzer.issues])

    def test_same_commit_requires_no_git_delta_lookup(self):
        self.candidate['fingerprints']['frozenCommit'] = REF
        self.analyzer.compare(self.candidate, self.reference)
        self.assertFalse(self.git_mock.called)

    def test_changed_toolchain_still_fails_across_commits(self):
        self.candidate['platforms'][0]['toolchains']['node'] = 'v24.99.0'
        self.analyzer.compare(self.candidate, self.reference)
        self.assertIn('toolchain_identity', [x['code'] for x in self.analyzer.issues])


if __name__ == '__main__':
    unittest.main(verbosity=2)
