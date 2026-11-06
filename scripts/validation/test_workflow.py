"""Offline tests for the default CELab Full CI workflow. Never invokes the real CI."""
import contextlib
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from analyze import Analyzer
from workflow import (WorkflowError, classify_run, ensure_linux, ensure_windows,
                      execute, validate_revision, verify_reference)

SHA = '1' * 40
TREE = '2' * 40
REF = '3' * 40
REF_TREE = '4' * 40
D = 'Ubuntu-24.04'


class FakeSystem:
    def __init__(self, source):
        self.source = Path(source)
        self.calls = []
        self.invocations = []
        self.bad_status = False
        self.index_flags = ''
        self.other_head = False
        self.ancestor = True
        self.linux_exists = False
        self.sdk_exists = True

    def command(self, argv, *, cwd=None, checked=True):
        self.calls.append(list(argv))
        code, out = 0, ''
        if argv[:3] == ['git', '-C', str(self.source)]:
            if '--show-toplevel' in argv:
                out = str(self.source)
            elif '--porcelain=v1' in argv:
                out = ' M engine/src/chess.cpp\n' if self.bad_status else ''
            elif 'ls-files' in argv:
                out = self.index_flags
            elif 'HEAD^{tree}' in argv:
                out = TREE
            elif argv[-1] == 'HEAD':
                out = SHA
            elif argv[-1] == REF + '^{tree}':
                out = REF_TREE
            elif '--is-ancestor' in argv:
                code = 0 if self.ancestor else 1
        elif argv[:4] == ['wsl.exe', '-d', D, '--exec']:
            arg = argv[4:]
            if arg[:2] == ['sh', '-c']:
                out = '/home/test'
            elif arg[:3] == ['bash', '-lc', 'source "$HOME/.nvm/nvm.sh" 2>/dev/null || true; node --version']:
                out = 'v24.10.0'
            elif arg[:3] == ['wslpath', '-a', '-u']:
                out = '/mnt/c/CELab'
            elif arg[:2] == ['test', '-f']:
                code = 0 if self.sdk_exists else 1
            elif arg[:2] == ['test', '-e']:
                code = 0 if self.linux_exists else 1
            elif arg[:3] == ['git', '-C', '/home/test/work/celab-ci-validation/' + SHA + '/linux-ci']:
                if arg[-1] == 'HEAD':
                    out = '0' * 40 if self.other_head else SHA
                elif arg[-1] == 'HEAD^{tree}':
                    out = TREE
                else:
                    out = ''
        elif argv[:2] == ['git', 'clone']:
            pass
        elif len(argv) > 2 and argv[0:2] == ['git', '-C'] and 'ci-validation' in argv[2]:
            if argv[-1] == 'HEAD':
                out = '0' * 40 if self.other_head else SHA
            elif argv[-1] == 'HEAD^{tree}':
                out = TREE
            else:
                out = ''
        p = subprocess.CompletedProcess(argv, code, out, '' if code == 0 else 'simulated failure')
        if checked and code:
            raise WorkflowError('Simulated command failed')
        return p

    def invoke(self, argv):
        self.invocations.append(argv)
        raise AssertionError('Full CI invocation must be explicitly mocked per test')


class WorkflowTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'checkout'
        (self.root / 'scripts/validation').mkdir(parents=True)
        (self.root / 'scripts/validation/run.py').write_text('# test fixture\n')
        self.reference = self.root / 'work/validation-efficiency/production-readiness-20261106T064606Z'
        self.reference.mkdir(parents=True)
        (self.reference / 'fingerprints.json').write_text(json.dumps({'frozenCommit': REF, 'tree': REF_TREE}))
        (self.reference / 'full-ci-summary.json').write_text('{}')
        (self.reference / 'evidence-manifest.json').write_text('{}')
        self.sys = FakeSystem(self.root)

    def test_selected_revision_and_tree(self):
        self.assertEqual(validate_revision(self.sys, self.root), (SHA, TREE))

    def test_dirty_source_fails_before_cloning(self):
        self.sys.bad_status = True
        with self.assertRaisesRegex(WorkflowError, 'uncommitted'):
            execute(self.root, self.reference, sysapi=self.sys, plan_only=True)
        self.assertFalse(any('clone' in args for args in self.sys.calls))

    def test_hidden_index_flags_rejected(self):
        self.sys.index_flags = 'h engine/src/unsafe.cpp\n'
        with self.assertRaisesRegex(WorkflowError, 'index flags'):
            validate_revision(self.sys, self.root)

    def test_reference_commit_tree_verified_separately(self):
        self.assertEqual(verify_reference(self.sys, self.root, self.reference, SHA), REF)
        with patch('analyze.git', return_value=REF_TREE):
            a = Analyzer(self.root, SHA, TREE)
            a.verify_tree({'tree': REF_TREE}, REF, 'reference', False)
            self.assertEqual(a.issues, [])
            a.verify_tree({'tree': REF_TREE}, SHA, 'candidate', True)
            self.assertIn('git_tree', [row['code'] for row in a.issues])

    def test_corrupt_reference_tree_rejected(self):
        (self.reference / 'fingerprints.json').write_text(json.dumps({'frozenCommit': REF, 'tree': '0' * 40}))
        with self.assertRaisesRegex(WorkflowError, 'Reference commit/tree'):
            verify_reference(self.sys, self.root, self.reference, SHA)

    def test_non_ancestor_reference_rejected(self):
        self.sys.ancestor = False
        with self.assertRaisesRegex(WorkflowError, 'not an ancestor'):
            verify_reference(self.sys, self.root, self.reference, SHA)

    def test_missing_reference_stops_before_clones(self):
        with self.assertRaisesRegex(WorkflowError, 'reference evidence missing'):
            execute(self.root, self.root / 'missing', sysapi=self.sys)
        self.assertFalse(any('clone' in args for args in self.sys.calls))

    def test_missing_sdk_fails_before_clones(self):
        self.sys.sdk_exists = False
        with self.assertRaisesRegex(WorkflowError, 'Emscripten SDK'):
            execute(self.root, self.reference, sysapi=self.sys)
        self.assertFalse(any('clone' in args for args in self.sys.calls))

    def test_plan_has_no_clone_and_no_coordinator(self):
        with contextlib.redirect_stdout(io.StringIO()) as out:
            self.assertEqual(execute(self.root, self.reference, sysapi=self.sys, plan_only=True), 0)
        self.assertIn('PLAN ONLY', out.getvalue())
        self.assertIn(SHA, out.getvalue())
        self.assertFalse(any('clone' in args for args in self.sys.calls))
        self.assertFalse(self.sys.invocations)

    def test_linux_checkout_wrong_revision_never_reset(self):
        self.sys.linux_exists = True
        self.sys.other_head = True
        with self.assertRaisesRegex(WorkflowError, 'wrong revision'):
            ensure_linux(self.sys, D, '/mnt/c/CELab', '/home/test/work/celab-ci-validation/' + SHA + '/linux-ci', SHA, TREE)
        self.assertFalse(any('reset' in args for args in self.sys.calls))

    def test_windows_checkout_wrong_revision_never_reset(self):
        dst = self.root / 'work/ci-validation' / SHA / 'windows-ci'
        dst.mkdir(parents=True)
        self.sys.other_head = True
        with self.assertRaisesRegex(WorkflowError, 'wrong revision'):
            ensure_windows(self.sys, self.root, dst, SHA, TREE)
        self.assertFalse(any('reset' in args for args in self.sys.calls))

    def test_linux_clone_is_local_and_does_not_fetch(self):
        ensure_linux(self.sys, D, '/mnt/c/CELab', '/home/test/work/celab-ci-validation/' + SHA + '/linux-ci', SHA, TREE)
        self.assertTrue(any(args[4:6] == ['git', 'clone'] for args in self.sys.calls))
        self.assertFalse(any('fetch' in args or 'pull' in args for args in self.sys.calls))

    def test_runner_invoked_once_with_exact_revision(self):
        def fake_invoke(argv):
            self.sys.invocations.append(argv)
            out = Path(argv[argv.index('--out') + 1]);out.mkdir(parents=True)
            (out / 'run.json').write_text(json.dumps({'classification':'authoritative_pass',
                'authoritative':True,'fullCiInvocations':1,'retries':0,'interventions':[]}))
            (out / 'full-attempt.json').write_text(json.dumps({'exitCode':0}))
            (out / 'raw-ci').mkdir()
            (out / 'raw-ci/report.json').write_text(json.dumps(full_report()))
            return 0
        self.sys.invoke = fake_invoke
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(execute(self.root, self.reference, sysapi=self.sys), 0)
        self.assertEqual(len(self.sys.invocations), 1)
        argv = self.sys.invocations[0]
        self.assertIn('--execute-full', argv)
        self.assertEqual(argv[argv.index('--expected-commit')+1], SHA)
        self.assertNotIn('-ReportRoot', argv)


def full_report():
    return {'mode':'full','authoritative':True,'verdict':'passed',
            'steps':[{'id':str(i),'status':'passed','selected':True,'exitCode':0} for i in range(54)],
            'workerExitEvidence':{'exits':{'windows':0,'linux':0}}}


class ClassificationTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.out=Path(self.temp.name);(self.out/'raw-ci').mkdir()
        (self.out/'raw-ci/report.json').write_text(json.dumps(full_report()))
        (self.out/'full-attempt.json').write_text(json.dumps({'exitCode':0}))
        self.run={'authoritative':True,'classification':'authoritative_pass',
                  'fullCiInvocations':1,'retries':0,'interventions':[]}

    def test_success(self):
        self.assertEqual(classify_run(self.out,self.run,0),('authoritative_pass',0))

    def test_reference_change_requires_review_not_approval(self):
        self.run.update(classification='fingerprint_mismatch',authoritative=False)
        self.assertEqual(classify_run(self.out,self.run,1),('reference_review_required',2))

    def test_broken_evidence_cannot_be_review_only(self):
        self.run.update(classification='evidence_incomplete',authoritative=False)
        self.assertEqual(classify_run(self.out,self.run,1),('validation_failed_or_incomplete',1))

    def test_missing_gate_is_failure(self):
        r=full_report();r['steps'].pop()
        (self.out/'raw-ci/report.json').write_text(json.dumps(r))
        self.run.update(classification='fingerprint_mismatch',authoritative=False)
        self.assertEqual(classify_run(self.out,self.run,1)[1],1)

    def test_nonzero_worker_exit_is_failure(self):
        r=full_report();r['workerExitEvidence']['exits']['linux']=2
        (self.out/'raw-ci/report.json').write_text(json.dumps(r))
        self.run.update(classification='fingerprint_mismatch',authoritative=False)
        self.assertEqual(classify_run(self.out,self.run,1)[1],1)

    def test_nonzero_coordinator_exit_is_failure(self):
        (self.out/'full-attempt.json').write_text(json.dumps({'exitCode':1}))
        self.run.update(classification='fingerprint_mismatch',authoritative=False)
        self.assertEqual(classify_run(self.out,self.run,1)[1],1)

    def test_missing_raw_report_is_failure(self):
        (self.out/'raw-ci/report.json').unlink()
        self.assertEqual(classify_run(self.out,self.run,1)[1],1)

    def test_extra_invocation_is_failure(self):
        self.run.update(fullCiInvocations=2,classification='fingerprint_mismatch',authoritative=False)
        self.assertEqual(classify_run(self.out,self.run,1)[1],1)

    def test_intervention_is_failure(self):
        self.run.update(interventions=['repair'],classification='fingerprint_mismatch',authoritative=False)
        self.assertEqual(classify_run(self.out,self.run,1)[1],1)


if __name__=='__main__':
    unittest.main(verbosity=2)
