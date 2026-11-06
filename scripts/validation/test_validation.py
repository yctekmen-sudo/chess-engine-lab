"""Focused B1 tests. Every runner execution uses a fake system, never Full CI."""
import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from analyze import analyze, resolve_report
from evidence import EvidenceError, bridge, digest, manifest, read_json, stable_records, write_json
import run as runner

BASE = Path(os.environ.get('CELAB_VALIDATION_EVIDENCE', Path.cwd() / 'work/validation-efficiency')).resolve()
A2 = BASE / 'codex-baseline-a2-20261106T152634Z'
A1 = BASE / 'codex-baseline-20261106T150936Z'
REFERENCE = BASE / 'production-readiness-20261106T064606Z'
COMMIT = 'bcfe5082d67a5bfc78f6c763f47897673633ee09'
TREE = '934b5733357ce1c6274c6a3a0fb0fdb12ea508f1'
SOURCE = A2 / 'windows-ci'


class CapturedEvidence(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not A2.is_dir() or not REFERENCE.is_dir():
            raise RuntimeError('Required captured evidence missing; no silent skips or replacement fixture')
        cls.raw_relative = resolve_report(A2).relative_to(A2)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='CELab B1 evidence ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'candidate'
        self.root.mkdir()
        for name in ['fingerprints.json', 'deterministic-test-fingerprints.json', 'evidence-manifest.json', 'full-attempt.json']:
            shutil.copyfile(A2 / name, self.root / name)
        shutil.copytree(A2 / 'full-extra', self.root / 'full-extra')
        self.raw = self.root / self.raw_relative
        shutil.copytree(A2 / self.raw_relative, self.raw)

    def result(self, **overrides):
        options = dict(evidence_root=self.root, reference_root=REFERENCE, source_repo=SOURCE,
                       expected_commit=COMMIT, expected_tree=TREE)
        options.update(overrides)
        return analyze(**options)

    def seal(self):
        write_json(self.root / 'evidence-manifest.json', manifest(self.root))

    def edit(self, path, action):
        value = read_json(path)
        action(value)
        write_json(path, value)

    def sync_worker(self, platform):
        """Reseal matching redundant copies so tests reach the underlying rules."""
        worker = read_json(self.raw / platform / 'report.json')
        complete = dict(worker, sourceFinal=read_json(self.raw / (platform + '-final-source.json')))
        combined = read_json(self.raw / 'report.json')
        combined['platforms'] = [complete if p['platform'] == platform else p for p in combined['platforms']]
        combined['steps'] = [s for p in combined['platforms'] for s in
                             [dict(s, id=p['platform'] + '/' + s['id']) for s in p['steps']]]
        write_json(self.raw / 'report.json', combined)
        sidecar = read_json(self.root / 'fingerprints.json')
        sidecar['sourceIdentities'][platform] = {k: complete[k] for k in ['sourceBefore', 'sourceAfter', 'sourceFinal']}
        sidecar['platforms'][platform] = {k: complete[k] for k in ['host', 'toolchains', 'harness', 'workflows']}
        write_json(self.root / 'fingerprints.json', sidecar)

    def reject(self, result, code=None):
        self.assertFalse(result['authoritative'])
        self.assertNotEqual(result['classification'], 'authoritative_pass')
        if code:
            self.assertIn(code, [i['code'] for i in result['discrepancies']])

    def test_a2_authoritative_from_source_evidence(self):
        result = self.result(evidence_root=A2)
        self.assertEqual(result['classification'], 'authoritative_pass', result['discrepancies'])
        plan = bridge(SOURCE, 'contract')['plans']
        self.assertEqual(len(result['steps']), sum(map(len, plan.values())))
        self.assertEqual(len(result['steps']), 54)
        sets = [r for r in result['comparisons'] if 'normalizedSha256' in r]
        self.assertEqual(len(sets), 7)
        for row in sets:
            original = A2 / 'full-extra' / row['platform'] / row['preset'] / 'LastTest.log'
            self.assertEqual(row['recordCount'], len(stable_records(original)))
            self.assertTrue(row['matchesAcceptedReference'])

    def test_a1_cannot_pass_without_original_coordinator_report(self):
        result = self.result(evidence_root=A1)
        self.reject(result, 'coordinator_exit')
        self.assertEqual(result['classification'], 'validation_failed')
        self.assertEqual(result['steps'], [])

    def test_missing_gate(self):
        self.edit(self.raw / 'report.json', lambda d: d['steps'].pop())
        self.seal()
        self.reject(self.result(), 'gate_inventory')

    def test_duplicate_gate(self):
        self.edit(self.raw / 'report.json', lambda d: d['steps'].append(copy.deepcopy(d['steps'][0])))
        self.seal()
        self.reject(self.result(), 'duplicate_gates')

    def test_nonzero_worker_exit(self):
        self.edit(self.raw / 'worker-exits.json', lambda d: d['exits'].update(windows=1))
        self.seal()
        self.assertEqual(self.result()['classification'], 'validation_failed')

    def test_missing_worker_exit(self):
        (self.raw / 'worker-exits.json').unlink()
        self.reject(self.result(), 'unreadable_json')

    def test_recorded_coordinator_process_failure_overrides_passing_report(self):
        self.edit(self.root / 'full-attempt.json', lambda d: d.update(exitCode=1))
        self.seal()
        self.assertEqual(self.result()['classification'], 'validation_failed')

    def test_missing_coordinator_invocation_records(self):
        (self.raw / 'coordinator.json').unlink()
        self.seal()
        self.reject(self.result(), 'missing_coordinator_invocations')

    def test_failed_combine_process_cannot_pass(self):
        self.edit(self.raw / 'coordinator.json', lambda d: d[-1].update(exitCode=1, status='failed'))
        self.seal()
        self.assertEqual(self.result()['classification'], 'validation_failed')

    def test_missing_individual_coordinator_invocation(self):
        self.edit(self.raw / 'coordinator.json', lambda d: d.pop(2))
        self.seal()
        self.reject(self.result(), 'coordinator_invocation_inventory')

    def test_external_unsealed_report_cannot_pass(self):
        self.reject(self.result(report_dir=resolve_report(A2)), 'unsealed_external_report')

    def test_skipped_required_gate(self):
        self.edit(self.raw / 'report.json', lambda d: d['steps'][0].update(status='skipped'))
        self.seal()
        self.reject(self.result(), 'combined_gate_copy')

    def test_failed_gate(self):
        self.edit(self.raw / 'report.json', lambda d: d['steps'][0].update(status='failed', exitCode=1))
        self.seal()
        self.assertEqual(self.result()['classification'], 'validation_failed')

    def test_wrong_source_sha(self):
        self.edit(self.raw / 'report.json', lambda d: d.update(commit='0' * 40))
        self.seal()
        self.reject(self.result(), 'source_commit')

    def test_wrong_expected_tree(self):
        result = self.result(expected_tree='0' * 40)
        self.reject(result, 'git_tree')
        self.assertEqual(result['classification'], 'fingerprint_mismatch')

    def test_wrong_recorded_tree(self):
        self.edit(self.root / 'fingerprints.json', lambda d: d.update(tree='0' * 40))
        self.seal()
        self.reject(self.result(), 'git_tree')

    def test_changed_deterministic_raw_output(self):
        path = self.root / 'full-extra/windows/native-debug/LastTest.log'
        path.write_text(path.read_text().replace('initial depth 1: 20', 'initial depth 1: 21'))
        self.seal()
        self.reject(self.result(), 'deterministic_records')

    def test_changed_declared_deterministic_hash(self):
        self.edit(self.root / 'deterministic-test-fingerprints.json',
                  lambda d: d['records'][0].update(normalizedSha256='0' * 64))
        self.seal()
        self.reject(self.result(), 'normalized_file_hash')

    def test_missing_fingerprint_file(self):
        (self.root / 'fingerprints.json').unlink()
        self.reject(self.result(), 'unreadable_json')

    def test_missing_deterministic_fingerprint_file(self):
        (self.root / 'deterministic-test-fingerprints.json').unlink()
        self.reject(self.result(), 'missing_deterministic_fingerprints')

    def test_missing_original_deterministic_log(self):
        (self.root / 'full-extra/linux/wasm-debug/LastTest.log').unlink()
        self.reject(self.result(), 'unreadable_deterministic_log')

    def test_missing_required_test_summary(self):
        self.edit(self.raw / 'windows/report.json', lambda d: next(
            s for s in d['steps'] if s['id'] == 'gui-test').pop('testResults'))
        self.sync_worker('windows')
        self.seal()
        self.reject(self.result(), 'test_results_copy')

    def test_corrupted_test_log(self):
        (self.raw / 'windows/gui-test.log').write_text('process exited zero but no test summary\n')
        self.seal()
        self.reject(self.result(), 'test_log_incomplete')

    def test_corrupt_report(self):
        (self.raw / 'report.json').write_text('{ truncated')
        self.seal()
        self.reject(self.result(), 'unreadable_json')

    def test_duplicate_json_key(self):
        (self.raw / 'report.json').write_text('{"authoritative":true,"authoritative":false}')
        self.seal()
        self.reject(self.result(), 'unreadable_json')

    def test_timing_variables_do_not_change_classification(self):
        for path in [self.raw / 'report.json', self.raw / 'windows/report.json']:
            def change(d):
                d['durationMs'] = 123456
                if 'platforms' in d:
                    d['platforms'][0]['durationMs'] = 123456
            self.edit(path, change)
        for platform in ['windows', 'linux']:
            for path in (self.root / 'full-extra' / platform).glob('*/LastTest.log'):
                import re
                path.write_text(re.sub(r'\bseconds=[0-9.eE+-]+', 'seconds=987.654', path.read_text()))
                meta = self.root / 'deterministic-test-fingerprints.json'
                data = read_json(meta)
                for row in data['records']:
                    if row['platform'] == platform and row['preset'] == path.parent.name:
                        row['rawLog']['sha256'] = digest(path)
                        row['rawLog']['bytes'] = path.stat().st_size
                write_json(meta, data)
        self.seal()
        result = self.result()
        self.assertEqual(result['classification'], 'authoritative_pass', result['discrepancies'])

    def test_changed_toolchain(self):
        self.edit(self.raw / 'windows/report.json', lambda d: d['toolchains'].update(node='v24.99.0'))
        self.sync_worker('windows')
        self.seal()
        self.reject(self.result(), 'toolchain_identity')

    def test_changed_evaluator_blob(self):
        def change(d):
            name = next(k for k in d['sourceEvaluatorConfigurationFiles'] if '/evaluation/' in k)
            d['sourceEvaluatorConfigurationFiles'][name]['gitBlob'] = '0' * 40
        self.edit(self.root / 'fingerprints.json', change)
        self.seal()
        self.reject(self.result(), 'source_blob')

    def test_dirty_source_cannot_pass(self):
        flags = [{'path': 'hidden.cpp', 'assumeUnchanged': True, 'skipWorktree': False}]
        for name in ['windows-source.json', 'windows-final-source.json']:
            self.edit(self.raw / name, lambda d: d.update(indexFlags=flags))
        self.edit(self.raw / 'windows/report.json', lambda d: [d[k].update(indexFlags=flags)
                                                             for k in ['sourceBefore', 'sourceAfter']])
        self.sync_worker('windows')
        self.seal()
        self.reject(self.result(), 'unclean_source')

    def test_skipped_gate_with_consistent_copies_cannot_pass(self):
        self.edit(self.raw / 'windows/report.json', lambda d: d['steps'][0].update(status='skipped'))
        self.sync_worker('windows')
        self.seal()
        self.reject(self.result(), 'gate_not_passed')

    def test_empty_gate_inventory_cannot_pass(self):
        for platform in ['windows', 'linux']:
            self.edit(self.raw / platform / 'report.json', lambda d: d.update(steps=[]))
            self.sync_worker(platform)
        self.seal()
        self.reject(self.result(), 'gate_inventory')

    def test_missing_binary_fingerprint(self):
        self.edit(self.root / 'fingerprints.json', lambda d: d['windowsBuildArtifacts'].pop(
            next(k for k in d['windowsBuildArtifacts'] if k.endswith('/material_evaluator.dll'))))
        self.seal()
        self.reject(self.result(), 'build_artifact_inventory')

    def test_missing_reference_is_explicit_failure(self):
        self.reject(self.result(reference_root=self.root / 'absent-reference'), 'invalid_evidence')

    def test_missing_normalized_file_cannot_pass(self):
        row = read_json(self.root / 'deterministic-test-fingerprints.json')['records'][0]
        (self.root / row['recordsPath']).unlink()
        self.seal()
        self.reject(self.result(), 'missing_normalized_file')

    def test_integrity_corruption(self):
        with (self.raw / 'windows/gui-build.log').open('a') as stream:
            stream.write('tampered\n')
        self.reject(self.result(), 'evidence_hash_mismatch')

    def test_reproducible_analyzer_output(self):
        self.assertEqual(self.result(), self.result())


class FakeSystem:
    def __init__(self, code=0, fail_preflight=False, fail_capture=False):
        self.calls, self.code, self.fail_preflight, self.fail_capture = [], code, fail_preflight, fail_capture

    def preflight(self, options, output):
        if self.fail_preflight:
            raise EvidenceError('synthetic preflight failure')
        return dict(reportBase='captured-evidence', reportDirectoriesBefore=[])

    def invoke(self, args, cwd, log):
        self.calls.append((args, cwd))
        Path(log).write_text('mock coordinator only; no real CI\n')
        return self.code

    def locate(self, preflight):
        return resolve_report(A2)

    def capture(self, options, raw, output, attempt):
        if self.fail_capture:
            raise EvidenceError('synthetic packaging failure')
        for name in ['fingerprints.json', 'deterministic-test-fingerprints.json']:
            shutil.copyfile(A2 / name, output / name)
        shutil.copytree(A2 / 'full-extra', output / 'full-extra')
        shutil.copytree(raw, output / 'raw-ci')
        write_json(output / 'evidence-manifest.json', manifest(output))
        return output / 'raw-ci'


class RunnerControlFlow(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='CELab runner B1 ')
        self.addCleanup(self.temp.cleanup)
        self.options = dict(windows_repo=str(SOURCE), linux_repo='/home/user/validation with spaces',
                            expected_commit=COMMIT, expected_tree=TREE, reference_root=str(REFERENCE),
                            out=str(Path(self.temp.name) / 'package'), distribution='Ubuntu-24.04',
                            linux_node='24.10.0', emsdk_root='/home/user/toolchains/emsdk', node='node')

    def test_default_is_plan_only_without_preflight_or_processes(self):
        system = FakeSystem()
        with patch.object(system, 'preflight', side_effect=AssertionError('must not run')):
            result = runner.run(self.options, system=system)
        self.assertEqual(result['fullCiInvocations'], 0)
        self.assertEqual(result['status'], 'planned_execution_disabled')
        self.assertEqual(system.calls, [])

    def test_command_default_report_location(self):
        command = runner.coordinator_command(self.options)
        self.assertEqual(command[:8], ['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass',
                                      '-File', 'scripts/ci/run-all.ps1', '-Mode', 'Full'])
        self.assertNotIn('-ReportRoot', command)
        self.assertEqual(command[command.index('-LinuxRepository') + 1], self.options['linux_repo'])

    def test_preflight_prevents_execution(self):
        system = FakeSystem(fail_preflight=True)
        result = runner.run(self.options, execute=True, system=system)
        self.assertEqual(system.calls, [])
        self.assertEqual(result['fullCiInvocations'], 0)
        self.assertFalse(result['authoritative'])

    def test_success_uses_captured_evidence_one_mock_invocation(self):
        system = FakeSystem()
        result = runner.run(self.options, execute=True, system=system)
        self.assertEqual(len(system.calls), 1)
        self.assertTrue(result['authoritative'], result)
        self.assertEqual(result['classification'], 'authoritative_pass')

    def test_failure_never_retries_even_with_passing_report(self):
        system = FakeSystem(code=1)
        result = runner.run(self.options, execute=True, system=system)
        self.assertEqual(len(system.calls), 1)
        self.assertEqual(result['classification'], 'validation_failed')
        self.assertFalse(result['authoritative'])

    def test_packaging_failure_never_retries(self):
        system = FakeSystem(fail_capture=True)
        result = runner.run(self.options, execute=True, system=system)
        self.assertEqual(len(system.calls), 1)
        self.assertFalse(result['authoritative'])
        attempt = read_json(Path(self.options['out']) / 'full-attempt.json')
        self.assertEqual(attempt['exitCode'], 0)
        self.assertIsNotNone(attempt['finishedUTC'])

    def test_existing_output_never_overwritten(self):
        Path(self.options['out']).mkdir()
        system = FakeSystem()
        with self.assertRaises(FileExistsError):
            runner.run(self.options, execute=True, system=system)
        self.assertEqual(system.calls, [])

    def test_clean_identity_rejects_tree_commit_and_flags(self):
        good = dict(commit=COMMIT, dirtyTracked=[], untracked=[], indexFlags=[])
        runner.clean_identity(good, COMMIT, TREE, TREE)
        for bad, tree in [(dict(good, commit='0' * 40), TREE), (good, '0' * 40),
                          (dict(good, indexFlags=[{'path': 'hidden'}]), TREE),
                          (dict(good, dirtyTracked=['modified.cpp']), TREE)]:
            with self.assertRaises(EvidenceError):
                runner.clean_identity(bad, COMMIT, TREE, tree)

    def test_real_packager_round_trip_with_captured_wsl_probe(self):
        import base64
        output = Path(self.options['out'])
        output.mkdir()
        linux_files = {}
        for path in (A2 / 'full-extra/linux').glob('*/*'):
            if path.is_file() and path.name in ['LastTest.log', 'LastTestsFailed.log', 'CTestCostData.txt']:
                linux_files[path.relative_to(A2 / 'full-extra/linux').as_posix()] = base64.b64encode(path.read_bytes()).decode()
        linux = dict(files=linux_files, artifacts=read_json(A2 / 'fingerprints.json')['linuxBuildArtifacts'])
        system = runner.LocalSystem()
        attempt = dict(command=runner.coordinator_command(self.options), exitCode=0, durationSeconds=1)
        with patch.object(system, 'wsl', return_value=json.dumps(linux)):
            copied = system.capture(self.options, resolve_report(A2), output, attempt)
        result = analyze(output, REFERENCE, SOURCE, COMMIT, TREE, report_dir=copied)
        self.assertEqual(result['classification'], 'authoritative_pass', result['discrepancies'])
        self.assertEqual(len(read_json(output / 'deterministic-test-fingerprints.json')['records']), 7)


if __name__ == '__main__':
    unittest.main(verbosity=2)
