"""Offline C1 regression tests. No Full CI, builds, network, or production writes."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from focused import (FocusedError, collect_paths, normalize_path, read_report,
                     run, select, System)


def git(path, *args):
    r = subprocess.run(['git', '-C', str(path), *args], capture_output=True, text=True)
    if r.returncode:
        raise AssertionError(r.stderr)
    return r.stdout.strip()


class SyntheticWindows(System):
    def __init__(self, behavior='pass'):
        self.behavior = behavior
        self.invocations = []
        self.python_invocations = []
        self.plan = [dict(id='harness-tests', selected=True),
                     dict(id='native-debug-configure', selected=True),
                     dict(id='native-release-ctest', selected=False)]

    def is_windows(self):
        return True

    def run(self, args, *, cwd, checked=True, binary=False):
        if args[:2] == ['node', str(cwd / 'scripts/dev/policy-plan.mjs')]:
            return subprocess.CompletedProcess(args, 0, json.dumps(self.plan), '')
        return super().run(args, cwd=cwd, checked=checked, binary=binary)

    def invoke(self, args, cwd, logfile):
        if args[0] == sys.executable:
            self.python_invocations.append(list(args))
            if self.behavior == 'python_skip':
                logfile.write_text('Ran 27 tests in 0.012s\n\nOK (skipped=1)\n', encoding='utf-8')
                return 0
            if self.behavior == 'python_fail':
                logfile.write_text('Ran 40 tests in 0.012s\n\nFAILED (failures=1)\n', encoding='utf-8')
                return 1
            if self.behavior == 'python_reduced':
                logfile.write_text('Ran 1 test in 0.012s\n\nOK\n', encoding='utf-8')
                return 0
            logfile.write_text('Ran 40 tests in 0.012s\n\nOK\n', encoding='utf-8')
            return 0
        self.invocations.append(list(args))
        report = Path(args[args.index('--report') + 1]); report.parent.mkdir(parents=True, exist_ok=True)
        info = {'commit': git(cwd, 'rev-parse', 'HEAD'), 'trackedFingerprint': 'a' * 64,
                'indexFingerprint': 'b' * 64, 'untrackedFingerprint': 'c' * 64}
        steps = [dict(**row, status='passed' if row['selected'] else 'not_selected',
                      **({'exitCode': 0} if row['selected'] else {})) for row in self.plan]
        output = dict(schemaVersion=1, platform='windows', mode='quick', authoritative=False,
                      checksVerdict='passed', sourceBefore=info, sourceAfter=info.copy(), steps=steps)
        if self.behavior == 'gate_failure':
            steps[1]['status'] = 'failed'; steps[1]['exitCode'] = 1
            output['checksVerdict'] = 'failed'
        if self.behavior == 'gate_blocked':
            steps[1]['status'] = 'blocked'; steps[1].pop('exitCode')
            output['checksVerdict'] = 'blocked'
        if self.behavior == 'missing_gate':
            steps.pop(1)
        if self.behavior == 'invalid_gate_type':
            steps[0] = None
        if self.behavior == 'source_changed':
            output['sourceAfter']['trackedFingerprint'] = 'f' * 64
        if self.behavior == 'wrong_commit':
            output['sourceBefore']['commit'] = 'f' * 40
        if self.behavior == 'malformed':
            report.write_text('{not valid')
        elif self.behavior != 'no_report':
            report.write_text(json.dumps(output))
        logfile.write_text('synthetic worker log')
        if self.behavior == 'gate_failure':
            return 1
        if self.behavior in ('gate_blocked', 'no_report'):
            return 2
        return 0


class SelectionTests(unittest.TestCase):
    def test_native_selects_dependency_and_qualifies_wasm_gap(self):
        s = select({'engine/src/search/search.cpp'})
        self.assertEqual(s.areas, ('native', 'controller'))
        self.assertEqual(s.linux_pending, ('linux-controller', 'linux-native', 'wasm'))

    def test_gui_requires_smoke_and_linux_e2e(self):
        s = select({'gui/src/App.tsx'})
        self.assertEqual(s.areas, ('gui', 'smoke'))
        self.assertEqual(s.linux_pending, ('e2e', 'linux-gui'))

    def test_controller_requires_smoke(self):
        self.assertEqual(select({'controller/src/index.ts'}).areas, ('controller', 'smoke'))

    def test_ci_policy_change_includes_windows_and_linux_gap(self):
        s = select({'scripts/ci/suite.mjs'})
        self.assertEqual(s.areas, ('infrastructure', 'native', 'controller', 'gui', 'smoke'))
        self.assertEqual(s.linux_pending, ('e2e', 'linux-controller', 'linux-gui', 'linux-infrastructure', 'linux-native', 'wasm'))

    def test_local_python_regressions_selected(self):
        a = select({'scripts/dev/focused.py'})
        b = select({'scripts/validation/analyze.py'})
        self.assertEqual(a.python_checks, ('c1-python-regressions',))
        self.assertEqual(b.python_checks, ('validation-cross-revision-regressions',
                                           'validation-workflow-regressions'))
        self.assertIn('validation-captured-evidence-review', b.linux_pending)

    def test_controller_linux_gap_recorded(self):
        self.assertEqual(select({'controller/src/index.ts'}).linux_pending,
                         ('linux-controller',))

    def test_documentation_only_selects_infrastructure(self):
        self.assertEqual(select({'docs/local-ci.md'}).areas, ('infrastructure',))

    def test_unknown_file_blocks_inference(self):
        with self.assertRaisesRegex(FocusedError, 'Unclassified'):
            select({'random-process.py'})

    def test_unknown_override_records_gap(self):
        s = select({'unknown.py'}, ('infrastructure',))
        self.assertEqual(s.linux_pending, ('unclassified-changes:unknown.py',))

    def test_explicit_area_override_records_skipped_inferred(self):
        s = select({'engine/src/search/search.cpp'}, ('infrastructure',))
        self.assertIn('windows-areas-not-selected:controller,native', s.linux_pending)
        self.assertIn('wasm', s.linux_pending)

    def test_linux_only_area_fails_closed(self):
        with self.assertRaisesRegex(FocusedError, 'Linux-only'):
            select({'gui/src/App.tsx'}, ('e2e',))

    def test_empty_selection_requires_explicit(self):
        with self.assertRaisesRegex(FocusedError, 'No changed'):
            select(set())

    def test_suspicious_git_paths_cannot_be_reclassified(self):
        for path in ('/tmp/foo', '../docs/README.md', 'docs/../engine/chess.cpp', ''):
            with self.subTest(path=path), self.assertRaises(FocusedError):
                normalize_path(path)


class GitAndRunTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='celab c1 test ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'repo'
        (self.root / 'scripts/ci').mkdir(parents=True)
        (self.root / 'scripts/dev').mkdir(parents=True)
        (self.root / 'engine/src').mkdir(parents=True)
        (self.root / 'scripts/ci/suite.mjs').write_text('// synthetic suite')
        (self.root / 'scripts/dev/policy-plan.mjs').write_text('// synthetic plan')
        (self.root / '.gitignore').write_text('work/\n')
        (self.root / 'README.md').write_text('baseline')
        git(self.root, 'init', '-q')
        git(self.root, 'config', 'user.email', 'test@example.invalid')
        git(self.root, 'config', 'user.name', 'CELab Test')
        git(self.root, 'add', '.')
        git(self.root, 'commit', '-qm', 'fixture')

    def test_uncommitted_and_untracked_paths_collected(self):
        (self.root / 'engine/src/search.cpp').write_text('untracked')
        (self.root / 'README.md').write_text('modified')
        sha, paths, base, tree = collect_paths(System(), self.root, 'HEAD')
        self.assertEqual(paths, ('README.md', 'engine/src/search.cpp'))
        self.assertEqual(sha, base)
        self.assertEqual(len(tree), 40)

    def test_staged_changes_collected(self):
        (self.root / 'README.md').write_text('staged')
        git(self.root, 'add', 'README.md')
        self.assertEqual(collect_paths(System(), self.root, 'HEAD')[1], ('README.md',))

    def test_branch_changes_collected_with_base_ref(self):
        before = git(self.root, 'rev-parse', 'HEAD')
        (self.root / 'engine/src/search.cpp').write_text('committed')
        git(self.root, 'add', '.')
        git(self.root, 'commit', '-qm', 'second')
        self.assertEqual(collect_paths(System(), self.root, before)[1], ('engine/src/search.cpp',))

    def test_index_flags_block_change_detection(self):
        git(self.root, 'update-index', '--assume-unchanged', 'README.md')
        with self.assertRaisesRegex(FocusedError, 'skip-worktree'):
            collect_paths(System(), self.root, 'HEAD')

    def test_plan_only_has_no_output_and_no_worker(self):
        (self.root / 'engine/src/search.cpp').write_text('untracked')
        sysapi = SyntheticWindows()
        code, result = run(self.root, system=sysapi)
        self.assertEqual(code, 0)
        self.assertEqual(result['status'], 'planned')
        self.assertFalse(result['authoritative'])
        self.assertEqual(result['linuxPending'], ['linux-controller', 'linux-native', 'wasm'])
        self.assertFalse(sysapi.invocations)
        self.assertFalse((self.root / 'work').exists())

    def test_selected_worker_pass_but_linux_pending_is_explicit(self):
        (self.root / 'engine/src/search.cpp').write_text('untracked')
        sysapi = SyntheticWindows()
        code, result = run(self.root, execute=True, system=sysapi)
        self.assertEqual(code, 0)
        self.assertEqual(result['status'], 'development_partial_pass')
        self.assertEqual(result['fullCiInvocations'], 0)
        self.assertFalse(result['authoritative'])
        self.assertEqual(result['selectedCount'], 2)
        self.assertEqual(len(sysapi.invocations), 1)
        self.assertEqual(sysapi.invocations[0][2], 'run')
        self.assertIn('--mode', sysapi.invocations[0]);self.assertIn('quick', sysapi.invocations[0])
        self.assertNotIn('full', sysapi.invocations[0])
        self.assertTrue(Path(result['evidenceDirectory'], 'summary.json').is_file())

    def test_human_summary_agrees_with_pass_and_worker_evidence(self):
        (self.root / 'README.md').write_text('updated')
        code, report = run(self.root, execute=True, system=SyntheticWindows())
        path = Path(report['evidenceDirectory'], 'summary.md')
        summary = path.read_text(encoding='utf-8')
        recorded = json.loads(Path(report['evidenceDirectory'], 'summary.json').read_text())
        self.assertEqual(code, 0)
        self.assertEqual(recorded['status'], report['status'])
        self.assertIn('**Verdict: `development_pass`**', summary)
        self.assertIn('**Windows Quick development scope only. NOT production qualification.**', summary)
        self.assertIn('| harness-tests | passed |', summary)
        self.assertIn('| native-debug-configure | passed |', summary)
        self.assertIn('None identified **within the selected C1 scope**', summary)
        self.assertIn('Windows worker report](windows/report.json)', summary)

    def test_human_summary_reports_partial_coverage_explicitly(self):
        (self.root / 'engine/src/search.cpp').write_text('untracked')
        code, report = run(self.root, execute=True, system=SyntheticWindows())
        summary = Path(report['evidenceDirectory'], 'summary.md').read_text()
        self.assertEqual(code, 0)
        self.assertEqual(report['status'], 'development_partial_pass')
        self.assertIn('`linux-native`', summary)
        self.assertIn('`wasm`', summary)
        self.assertIn('A partial pass is not full coverage.', summary)

    def test_human_summary_preserves_worker_failure(self):
        (self.root / 'README.md').write_text('updated')
        code, report = run(self.root, execute=True, system=SyntheticWindows('gate_failure'))
        summary = Path(report['evidenceDirectory'], 'summary.md').read_text()
        self.assertEqual((code, report['status']), (1, 'development_failed'))
        self.assertIn('**Verdict: `development_failed`**', summary)
        self.assertIn('## Failures to investigate', summary)
        self.assertIn('**native-debug-configure**: failed', summary)
        self.assertNotIn('**Verdict: `development_pass`**', summary)

    def test_human_summary_preserves_invalid_evidence_without_success_claim(self):
        (self.root / 'README.md').write_text('updated')
        code, report = run(self.root, execute=True, system=SyntheticWindows('malformed'))
        summary = Path(report['evidenceDirectory'], 'summary.md').read_text()
        self.assertEqual((code, report['status']), (2, 'evidence_invalid'))
        self.assertIn('**Verdict: `evidence_invalid`**', summary)
        self.assertIn('Worker report missing or malformed', summary)
        self.assertIn('No validated Windows gate details available.', summary)
        self.assertNotIn('| harness-tests | passed |', summary)

    def test_human_summary_shows_python_regression_counts(self):
        (self.root / 'scripts/dev/focused.py').write_text('# changed')
        code, report = run(self.root, execute=True, system=SyntheticWindows())
        summary = Path(report['evidenceDirectory'], 'summary.md').read_text()
        self.assertEqual(code, 0)
        self.assertIn('| c1-python-regressions | passed | 40 | 40 |', summary)
        self.assertIn('c1-python-regressions.log', summary)

    def test_docs_only_worker_pass_has_no_linux_pending(self):
        (self.root / 'README.md').write_text('updated')
        sysapi = SyntheticWindows()
        code, result = run(self.root, execute=True, system=sysapi)
        self.assertEqual(code, 0)
        self.assertEqual(result['status'], 'development_pass')
        self.assertEqual(result['windowsAreas'], ['infrastructure'])

    def test_c1_python_gate_runs_once_and_is_recorded(self):
        (self.root / 'scripts/dev/focused.py').write_text('# changed')
        sysapi = SyntheticWindows()
        code, result = run(self.root, execute=True, system=sysapi)
        self.assertEqual((code, result['status']), (0, 'development_pass'))
        self.assertEqual(len(sysapi.invocations), 1)
        self.assertEqual(len(sysapi.python_invocations), 1)
        self.assertEqual(result['pythonChecks'][0]['status'], 'passed')
        self.assertEqual(result['pythonChecks'][0]['passed'], 40)
        self.assertEqual(result['pythonChecks'][0]['minimumExpected'], 40)

    def test_validation_python_regressions_run_and_captured_gap_remains(self):
        (self.root / 'scripts/validation').mkdir(parents=True)
        (self.root / 'scripts/validation/workflow.py').write_text('# changed')
        sysapi = SyntheticWindows()
        code, result = run(self.root, execute=True, system=sysapi)
        self.assertEqual((code, result['status']), (0, 'development_partial_pass'))
        self.assertEqual(len(sysapi.python_invocations), 2)
        self.assertIn('validation-captured-evidence-review', result['coveragePending'])

    def test_python_regressions_failing_or_skipped_cannot_pass(self):
        (self.root / 'scripts/dev/focused.py').write_text('# changed')
        for mode, expected in [('python_skip', (2, 'development_blocked')),
                               ('python_fail', (1, 'development_failed'))]:
            with self.subTest(mode=mode):
                code, result = run(self.root, execute=True, system=SyntheticWindows(mode))
                self.assertEqual((code, result['status']), expected)

    def test_python_regression_coverage_reduction_blocks_acceptance(self):
        (self.root / 'scripts/dev/focused.py').write_text('# changed')
        code, result = run(self.root, execute=True, system=SyntheticWindows('python_reduced'))
        self.assertEqual((code, result['status']), (2, 'development_blocked'))
        self.assertEqual(result['pythonChecks'][0]['observedCount'], 1)
        self.assertEqual(result['pythonChecks'][0]['minimumExpected'], 40)

    def test_python_gate_source_mutation_detected(self):
        (self.root / 'scripts/dev/focused.py').write_text('# changed')
        class Mutator(SyntheticWindows):
            def invoke(self, args, cwd, logfile):
                code = super().invoke(args, cwd, logfile)
                if args[0] == sys.executable:
                    (cwd / 'scripts/dev/focused.py').write_text('# modified during tests')
                return code
        code, result = run(self.root, execute=True, system=Mutator())
        self.assertEqual((code, result['status']), (2, 'evidence_invalid'))

    def test_worker_failure_no_retry(self):
        (self.root / 'README.md').write_text('modified')
        sysapi = SyntheticWindows('gate_failure')
        code, result = run(self.root, execute=True, system=sysapi)
        self.assertEqual((code, result['status']), (1, 'development_failed'))
        self.assertEqual(len(sysapi.invocations), 1)

    def test_missing_report_not_success(self):
        (self.root / 'README.md').write_text('modified')
        code, result = run(self.root, execute=True, system=SyntheticWindows('no_report'))
        self.assertEqual((code, result['status']), (2, 'evidence_invalid'))

    def test_gate_missing_cannot_pass(self):
        (self.root / 'README.md').write_text('modified')
        code, result = run(self.root, execute=True, system=SyntheticWindows('missing_gate'))
        self.assertEqual((code, result['status']), (2, 'evidence_invalid'))

    def test_malformed_worker_step_has_structured_invalid_evidence(self):
        (self.root / 'README.md').write_text('modified')
        code, result = run(self.root, execute=True, system=SyntheticWindows('invalid_gate_type'))
        self.assertEqual((code, result['status']), (2, 'evidence_invalid'))
        self.assertTrue(Path(result['evidenceDirectory'], 'summary.json').is_file())

    def test_worker_blocked_cannot_pass(self):
        (self.root / 'README.md').write_text('modified')
        code, result = run(self.root, execute=True, system=SyntheticWindows('gate_blocked'))
        self.assertEqual((code, result['status']), (2, 'development_blocked'))

    def test_worker_source_change_not_success(self):
        (self.root / 'README.md').write_text('modified')
        code, result = run(self.root, execute=True, system=SyntheticWindows('source_changed'))
        self.assertEqual((code, result['status']), (2, 'evidence_invalid'))

    def test_worker_wrong_commit_not_success(self):
        (self.root / 'README.md').write_text('modified')
        code, result = run(self.root, execute=True, system=SyntheticWindows('wrong_commit'))
        self.assertEqual((code, result['status']), (2, 'evidence_invalid'))

    def test_unignored_output_is_rejected(self):
        (self.root / '.gitignore').write_text('')
        (self.root / 'README.md').write_text('modified')
        sysapi = SyntheticWindows()
        with self.assertRaisesRegex(FocusedError, 'Git ignored'):
            run(self.root, execute=True, system=sysapi)
        self.assertFalse(sysapi.invocations)

    def test_linux_host_execution_denied(self):
        (self.root / 'README.md').write_text('modified')
        # Simulate a Linux host even when this suite is run on Windows.
        # The production guard remains unchanged and must fail closed.
        with patch.object(System, 'is_windows', return_value=False):
            with self.assertRaisesRegex(FocusedError, 'Windows-only'):
                run(self.root, execute=True, system=System())

    def test_malformed_report_cannot_pass(self):
        (self.root / 'README.md').write_text('modified')
        code, result = run(self.root, execute=True, system=SyntheticWindows('malformed'))
        self.assertEqual((code, result['status']), (2, 'evidence_invalid'))


if __name__ == '__main__':
    unittest.main(verbosity=2)
