"""One-command CELab Full CI orchestration; no model calls, network fetches or retries.

Run from the committed tooling checkout, not from Codex. Distinct clean Windows/WSL
validation clones are created only under predictable, ignored work directories.
An approved cross-revision reference mismatch is *not* silently accepted.
"""
import argparse
import datetime as dt
import json
from pathlib import Path
import re
import subprocess
import sys
import uuid


class WorkflowError(RuntimeError):
    pass


class System:
    def command(self, argv, *, cwd=None, checked=True):
        p = subprocess.run(argv, cwd=cwd, capture_output=True, text=True, check=False)
        if checked and p.returncode:
            raise WorkflowError(f'Command failed (exit {p.returncode}): {argv!r}\n{p.stderr.strip()}')
        return p

    def invoke(self, argv):
        # Inherit the terminal; the original runner captures its own CI console.
        return subprocess.run(argv, check=False).returncode


def result(sysapi, argv, *, cwd=None):
    return sysapi.command(argv, cwd=cwd).stdout.strip()


def git(sysapi, root, *args):
    return result(sysapi, ['git', '-C', str(root), *args])


def wsl(sysapi, distro, *args):
    return result(sysapi, ['wsl.exe', '-d', distro, '--exec', *args])


def validate_revision(sysapi, root):
    if not (root / 'scripts/validation/run.py').is_file():
        raise WorkflowError('Call the installed wrapper from a complete CELab tooling checkout.')
    if not ((root / '.git').is_dir() or (root / '.git').is_file()):
        raise WorkflowError('Source Git metadata is missing or invalid.')
    if Path(git(sysapi, root, 'rev-parse', '--show-toplevel')).resolve() != root.resolve():
        raise WorkflowError('The validation tooling must be located in the Git repository root.')
    if git(sysapi, root, 'status', '--porcelain=v1', '--untracked-files=all'):
        raise WorkflowError('Source contains uncommitted or untracked changes. Commit or isolate changes before Full CI.')
    flags = git(sysapi, root, 'ls-files', '-v').splitlines()
    if any(row and (row[0].islower() or row[0] == 'S') for row in flags):
        raise WorkflowError('Source has assume-unchanged or skip-worktree index flags.')
    sha, tree = git(sysapi, root, 'rev-parse', 'HEAD'), git(sysapi, root, 'rev-parse', 'HEAD^{tree}')
    if not all(re.fullmatch('[0-9a-f]{40}', v) for v in (sha, tree)):
        raise WorkflowError('Expected full commit and tree object IDs.')
    return sha, tree


def verify_reference(sysapi, source, directory, sha):
    if not directory.is_dir():
        raise WorkflowError('Approved reference evidence missing: ' + str(directory))
    try:
        ref = json.loads((directory / 'fingerprints.json').read_text(encoding='utf-8'))
    except (OSError, ValueError) as error:
        raise WorkflowError('Approved reference fingerprint metadata is missing or invalid') from error
    frozen = ref.get('frozenCommit')
    if not isinstance(frozen, str) or not re.fullmatch('[0-9a-f]{40}', frozen):
        raise WorkflowError('Approved reference lacks a full source commit SHA.')
    if not (directory / 'full-ci-summary.json').is_file() or not (directory / 'evidence-manifest.json').is_file():
        raise WorkflowError('Approved reference lacks its original sealed Full CI evidence.')
    if git(sysapi, source, 'rev-parse', frozen + '^{tree}') != ref.get('tree'):
        raise WorkflowError('Reference commit/tree differs from the local Git object database.')
    if sysapi.command(['git', '-C', str(source), 'merge-base', '--is-ancestor', frozen, sha],
                      checked=False).returncode:
        raise WorkflowError('Reference commit is not an ancestor of the selected revision; select an approved reference.')
    return frozen


def ensure_windows(sysapi, source, destination, sha, tree):
    if not destination.exists():
        destination.parent.mkdir(parents=True, exist_ok=True)
        # No fetch/pull/reset: clone only committed local Git objects to an isolated destination.
        sysapi.command(['git', 'clone', '--no-hardlinks', '--no-checkout', str(source), str(destination)])
        sysapi.command(['git', '-C', str(destination), 'checkout', '--detach', sha])
    if git(sysapi, destination, 'rev-parse', 'HEAD') != sha or git(sysapi, destination, 'rev-parse', 'HEAD^{tree}') != tree:
        raise WorkflowError('Existing Windows validation checkout has the wrong revision; refusing to reset it.')
    if git(sysapi, destination, 'status', '--porcelain=v1', '--untracked-files=all'):
        raise WorkflowError('Existing Windows validation checkout is not clean.')


def ensure_linux(sysapi, distro, source_wsl, destination, sha, tree):
    # test -e can legitimately return nonzero; inspect with direct argv.
    present = sysapi.command(['wsl.exe', '-d', distro, '--exec', 'test', '-e', destination], checked=False)
    if present.returncode not in (0, 1):
        raise WorkflowError('Unable to inspect WSL validation checkout.')
    if present.returncode == 1:
        parent = destination.rsplit('/', 1)[0]
        wsl(sysapi, distro, 'mkdir', '-p', parent)
        wsl(sysapi, distro, 'git', 'clone', '--no-checkout', source_wsl, destination)
        wsl(sysapi, distro, 'git', '-C', destination, 'checkout', '--detach', sha)
    if wsl(sysapi, distro, 'git', '-C', destination, 'rev-parse', 'HEAD') != sha or \
       wsl(sysapi, distro, 'git', '-C', destination, 'rev-parse', 'HEAD^{tree}') != tree:
        raise WorkflowError('Existing WSL validation checkout has the wrong revision; refusing to reset it.')
    if wsl(sysapi, distro, 'git', '-C', destination, 'status', '--porcelain=v1', '--untracked-files=all'):
        raise WorkflowError('Existing WSL validation checkout is not clean.')


def read_run(out):
    try:
        return json.loads((out / 'run.json').read_text(encoding='utf-8'))
    except (OSError, ValueError) as error:
        raise WorkflowError('Runner did not produce valid run.json; inspect the preserved evidence directory.') from error


def classify_run(out, run, child_exit):
    """No automatic reference approval: new-revision differences require review."""
    attempt, report = {}, {}
    try:
        attempt = json.loads((out / 'full-attempt.json').read_text(encoding='utf-8'))
        report = json.loads((out / 'raw-ci/report.json').read_text(encoding='utf-8'))
    except (OSError, ValueError):
        pass
    exits = report.get('workerExitEvidence', {}).get('exits', {}) if isinstance(report.get('workerExitEvidence'), dict) else {}
    gates = report.get('steps')
    full_pass = (report.get('mode') == 'full' and report.get('authoritative') is True and
                 report.get('verdict') == 'passed' and isinstance(gates, list) and len(gates) == 54 and
                 all(isinstance(g, dict) and g.get('status') == 'passed' and g.get('selected') is True
                     and g.get('exitCode') == 0 for g in gates) and
                 exits == {'windows': 0, 'linux': 0} and attempt.get('exitCode') == 0 and
                 run.get('fullCiInvocations') == 1 and run.get('retries') == 0 and
                 run.get('interventions') == [])
    if child_exit == 0 and run.get('authoritative') is True and run.get('classification') == 'authoritative_pass' and full_pass:
        return 'authoritative_pass', 0
    if full_pass and run.get('classification') == 'fingerprint_mismatch':
        return 'reference_review_required', 2
    return 'validation_failed_or_incomplete', 1


def execute(source, reference, distro='Ubuntu-24.04', plan_only=False, sysapi=None):
    sysapi = sysapi or System()
    source = Path(source).resolve()
    sha, tree = validate_revision(sysapi, source)
    ref_commit = verify_reference(sysapi, source, Path(reference).resolve(), sha)
    home = wsl(sysapi, distro, 'sh', '-c', 'printf "%s" "$HOME"')
    if not home.startswith('/') or '\n' in home:
        raise WorkflowError('Unable to resolve an absolute WSL home directory.')
    emsdk = home.rstrip('/') + '/toolchains/emsdk'
    if sysapi.command(['wsl.exe', '-d', distro, '--exec', 'test', '-f', emsdk + '/emsdk_env.sh'],
                      checked=False).returncode != 0:
        raise WorkflowError('Expected Emscripten SDK not found: ' + emsdk)
    # nvm must be available even when invoked through a non-interactive WSL command.
    linux_node = wsl(sysapi, distro, 'bash', '-lc',
                     'source "$HOME/.nvm/nvm.sh" 2>/dev/null || true; node --version').strip().lstrip('v')
    if not re.fullmatch(r'24\.\d+\.\d+', linux_node):
        raise WorkflowError('Expected WSL Node.js 24 (resolved ' + linux_node + ').')
    win = source / 'work' / 'ci-validation' / sha / 'windows-ci'
    # WSL Git cannot follow a Windows gitdir: pointer in a linked worktree.
    # The isolated Windows clone contains the selected commit (even detached),
    # independently of the common repository's HEAD or advertised branches.
    linux_origin = win if (source / '.git').is_file() else source
    source_wsl = wsl(sysapi, distro, 'wslpath', '-a', '-u', str(linux_origin))
    linux = home.rstrip('/') + '/work/celab-ci-validation/' + sha + '/linux-ci'
    out = source / 'work' / 'validation-efficiency' / 'routine' / (
        dt.datetime.now(dt.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ') + '-' + uuid.uuid4().hex[:8])
    plan = dict(source=str(source), commit=sha, tree=tree, approvedReferenceCommit=ref_commit,
                windowsCheckout=str(win), linuxCheckout=linux, linuxCloneOrigin=str(linux_origin), distro=distro,
                emsdk=emsdk, linuxNode=linux_node, evidenceDirectory=str(out),
                executeFull=not plan_only)
    print(json.dumps(plan, sort_keys=True, indent=2), flush=True)
    if plan_only:
        print('PLAN ONLY: no validation clones, tests or coordinator invocations.', flush=True)
        return 0
    ensure_windows(sysapi, source, win, sha, tree)
    if linux_origin == win and not (win / '.git').is_dir():
        raise WorkflowError('WSL clone origin must be an ordinary Windows validation clone with a .git directory.')
    ensure_linux(sysapi, distro, source_wsl, linux, sha, tree)
    argv = [sys.executable, '-u', str(source / 'scripts/validation/run.py'),
            '--windows-repo', str(win), '--linux-repo', linux,
            '--expected-commit', sha, '--expected-tree', tree,
            '--reference-root', str(reference), '--out', str(out),
            '--emsdk-root', emsdk, '--distribution', distro, '--linux-node', linux_node,
            '--execute-full']
    child_exit = sysapi.invoke(argv)
    record = read_run(out)
    status, code = classify_run(out, record, child_exit)
    summary = dict(status=status, exitCode=code, runnerExitCode=child_exit,
                   selectedCommit=sha, selectedTree=tree, approvedReferenceCommit=ref_commit,
                   runnerClassification=record.get('classification'),
                   coordinatorInvocationCount=record.get('fullCiInvocations'),
                   fullCiAuthoritative=(status == 'authoritative_pass'),
                   crossRevisionReviewRequired=(status == 'reference_review_required'),
                   originalReportDirectory=record.get('originalReportDirectory'),
                   evidenceDirectory=str(out))
    (out / 'workflow-summary.json').write_text(json.dumps(summary, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    print(json.dumps(summary, indent=2, sort_keys=True), flush=True)
    if code == 2:
        print('Full CI reported PASS, but reference fingerprints differ. Review before accepting this revision.', flush=True)
    elif code == 1:
        print('Validation not accepted. Preserve evidence; investigate without automatic reruns.', flush=True)
    return code


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[2])
    p.add_argument('--reference-root', type=Path, default=None)
    p.add_argument('--distribution', default='Ubuntu-24.04')
    p.add_argument('--plan-only', action='store_true')
    args = p.parse_args(argv)
    reference = args.reference_root or args.source / 'work/validation-efficiency/production-readiness-20261106T064606Z'
    try:
        return execute(args.source, reference, args.distribution, args.plan_only)
    except (WorkflowError, OSError, ValueError) as error:
        print('Full CI workflow stopped: ' + str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
