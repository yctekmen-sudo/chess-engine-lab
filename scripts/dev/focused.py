"""CELab C1: deterministic Windows development checks using the existing Quick CI worker.

No commits, Git writes, fetches, source synchronization, Full CI or model calls.
This intentionally validates the current working tree, including local edits;
it is never an authoritative production CI result.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import uuid

AREAS = ('infrastructure', 'native', 'controller', 'gui', 'smoke')


class FocusedError(RuntimeError):
    pass


@dataclass(frozen=True)
class Selection:
    areas: tuple[str, ...]
    changed: tuple[str, ...]
    linux_pending: tuple[str, ...]
    explicit: bool
    python_checks: tuple[str, ...]


def normalize_path(path: str) -> str:
    # Git always uses repository-relative forward slashes. Do not silently
    # classify a suspicious path as documentation.
    path = path.replace('\\', '/')
    if not path or path.startswith('/') or path.startswith('../') or '/..' in path or '\x00' in path:
        raise FocusedError(f'Unsafe Git path: {path!r}')
    return path


def paths_to_areas(path: str) -> tuple[set[str], set[str]] | None:
    """Conservative selection: unknown paths must be explicitly classified."""
    p = normalize_path(path)
    if p == 'AGENTS.md' or p == 'README.md' or p == '.gitignore' or p.startswith('docs/'):
        return {'infrastructure'}, set()
    if p.startswith('scripts/dev/'):
        return {'infrastructure'}, set()
    if p.startswith('scripts/validation/'):
        return {'infrastructure'}, {'validation-captured-evidence-review'}
    if p.startswith('scripts/ci/'):
        return {'infrastructure', 'native', 'controller', 'gui', 'smoke'}, {'linux-infrastructure', 'linux-native', 'linux-controller', 'linux-gui', 'wasm', 'e2e'}
    if p.startswith('engine/') or p.startswith('tests/') or p in ('CMakeLists.txt', 'CMakePresets.json'):
        return {'native', 'controller'}, {'linux-native', 'linux-controller', 'wasm'}
    if p.startswith('controller/'):
        return {'controller', 'smoke'}, {'linux-controller'}
    if p.startswith('gui/'):
        return {'gui', 'smoke'}, {'linux-gui', 'e2e'}
    if p.startswith('.github/'):
        return {'infrastructure'}, set()
    return None


def select(paths: set[str], explicit: tuple[str, ...] | None = None) -> Selection:
    changed = tuple(sorted(map(normalize_path, paths)))
    inferred, pending, unknown, python_checks = set(), set(), [], set()
    for path in changed:
        if path.startswith('scripts/dev/'):
            python_checks.add('c1-python-regressions')
        if path.startswith('scripts/validation/'):
            python_checks.update(('validation-workflow-regressions', 'validation-cross-revision-regressions'))
        classified = paths_to_areas(path)
        if classified is None:
            unknown.append(path)
        else:
            a, l = classified
            inferred.update(a)
            pending.update(l)
    if explicit is not None:
        invalid = set(explicit) - set(AREAS)
        if invalid:
            raise FocusedError('Only Windows development areas supported by C1: ' + ', '.join(sorted(invalid)) +
                               '. Linux-only checks must be selected through a separately qualified workflow.')
        if not explicit:
            raise FocusedError('Explicit area list cannot be empty.')
        # An explicit area override may reduce coverage. Record the omitted
        # inferred areas for transparent review rather than claiming coverage.
        omitted = sorted(inferred - set(explicit))
        if omitted:
            pending.add('windows-areas-not-selected:' + ','.join(omitted))
        if unknown:
            pending.add('unclassified-changes:' + ','.join(unknown))
        areas = tuple(a for a in AREAS if a in explicit)
    else:
        if unknown:
            raise FocusedError('Unclassified source paths: ' + ', '.join(unknown) +
                               '. Select explicit --areas after reviewing the required coverage.')
        if not changed:
            raise FocusedError('No changed source paths relative to the selected base. Use --areas explicitly.')
        areas = tuple(a for a in AREAS if a in inferred)
    return Selection(areas, changed, tuple(sorted(pending)), explicit is not None, tuple(sorted(python_checks)))


class System:
    def run(self, args: list[str], *, cwd: Path, checked=True, binary=False):
        result = subprocess.run(args, cwd=cwd, capture_output=True, check=False,
                                text=not binary, encoding=None if binary else 'utf-8',
                                errors=None if binary else 'surrogateescape',
                                env={**os.environ, 'GIT_OPTIONAL_LOCKS': '0'})
        if checked and result.returncode:
            err = result.stderr.decode('utf-8', errors='replace') if binary else result.stderr
            raise FocusedError(f'Command failed (exit {result.returncode}): {args!r}\n{err.strip()}')
        return result

    def invoke(self, args: list[str], cwd: Path, logfile: Path) -> int:
        with logfile.open('wb') as out:
            return subprocess.run(args, cwd=cwd, check=False, stdout=out, stderr=subprocess.STDOUT).returncode

    def is_windows(self) -> bool:
        return sys.platform == 'win32'


def git(system: System, root: Path, *args: str, binary=False):
    return system.run(['git', *args], cwd=root, binary=binary).stdout


def resolve_root(system: System, path: Path) -> Path:
    root = Path(git(system, path, 'rev-parse', '--show-toplevel').strip()).resolve()
    if not (root / 'scripts/ci/suite.mjs').is_file():
        raise FocusedError('Expected CELab checkout containing scripts/ci/suite.mjs.')
    return root


def collect_paths(system: System, root: Path, ref: str) -> tuple[str, tuple[str, ...], str, str]:
    # Ensure the base object is pinned. Never fetch/checkout/reset.
    source_commit = git(system, root, 'rev-parse', 'HEAD').strip()
    base_commit = git(system, root, 'rev-parse', '--verify', ref + '^{commit}').strip()
    if not all(re.fullmatch('[a-f0-9]{40}', x) for x in (source_commit, base_commit)):
        raise FocusedError('Expected full Git commit SHAs for source and comparison base.')
    if system.run(['git', 'merge-base', '--is-ancestor', base_commit, source_commit],
                  cwd=root, checked=False).returncode != 0:
        raise FocusedError('Comparison base is not an ancestor of HEAD. Choose an explicit ancestor.')
    flags = git(system, root, 'ls-files', '-v', '-z', binary=True).split(b'\0')
    if any(row and (row[:1] == b'S' or 97 <= row[0] <= 122) for row in flags):
        raise FocusedError('Git skip-worktree or assume-unchanged flags prevent reliable change selection.')
    changed = git(system, root, 'diff', '--name-only', '--no-renames', '-z', base_commit, '--', binary=True)
    untracked = git(system, root, 'ls-files', '--others', '--exclude-standard', '-z', binary=True)
    items = {os.fsdecode(part) for raw in (changed, untracked) for part in raw.split(b'\0') if part}
    tree = git(system, root, 'rev-parse', 'HEAD^{tree}').strip()
    return source_commit, tuple(sorted(items)), base_commit, tree


def source_digest(system: System, root: Path) -> str:
    """Working-content and index guard across the complete C1 execution."""
    stage = git(system, root, 'ls-files', '--stage', '-z', binary=True)
    tracked = git(system, root, 'ls-files', '-z', binary=True).split(b'\0')
    untracked = git(system, root, 'ls-files', '--others', '--exclude-standard', '-z', binary=True).split(b'\0')
    digest = hashlib.sha256(stage)
    for raw in sorted(set(tracked + untracked)):
        if not raw:
            continue
        path = root / normalize_path(os.fsdecode(raw))
        digest.update(raw + b'\0')
        try:
            if path.is_symlink():
                payload = os.fsencode(os.readlink(path))
            else:
                payload = path.read_bytes()
        except FileNotFoundError:
            payload = b'<missing>'
        digest.update(hashlib.sha256(payload).digest())
    return digest.hexdigest()


def read_report(path: Path, system_exit: int, commit: str, expected: list[dict]) -> tuple[str, dict]:
    try:
        report = json.loads(path.read_text(encoding='utf-8'))
    except (OSError, ValueError) as exc:
        raise FocusedError('Worker report missing or malformed; inspect coordinator.log.') from exc
    if not isinstance(report, dict):
        raise FocusedError('Worker report must be a JSON object.')
    if any(report.get(k) != v for k, v in [('schemaVersion', 1), ('platform', 'windows'),
                                           ('mode', 'quick'), ('authoritative', False)]):
        raise FocusedError('Worker report does not identify non-authoritative Windows Quick mode.')
    steps = report.get('steps')
    if not isinstance(steps, list) or not steps:
        raise FocusedError('Worker report lacks the required step inventory.')
    if any(not isinstance(s, dict) or not isinstance(s.get('id'), str) for s in steps):
        raise FocusedError('Malformed worker gate inventory.')
    selected = [s for s in steps if s.get('selected') is True]
    if not selected:
        raise FocusedError('Worker reported no valid selected tests.')
    if len(set(s['id'] for s in steps)) != len(steps):
        raise FocusedError('Duplicate worker step IDs.')
    if any(type(s.get('selected')) is not bool for s in steps):
        raise FocusedError('Worker selected flags are missing or malformed.')
    if [(s['id'], s['selected']) for s in steps] != [(s['id'], s['selected']) for s in expected]:
        raise FocusedError('Worker gate inventory differs from the existing CI policy.')
    if any(s.get('status') != 'not_selected' for s in steps if s.get('selected') is False):
        raise FocusedError('Unselected worker step has an unexpected status.')
    before, after = report.get('sourceBefore'), report.get('sourceAfter')
    if not isinstance(before, dict) or not isinstance(after, dict):
        raise FocusedError('Worker source identity evidence is missing.')
    fields = ('commit', 'indexFingerprint', 'trackedFingerprint', 'untrackedFingerprint')
    if before.get('commit') != commit or any(not before.get(k) or before.get(k) != after.get(k) for k in fields):
        raise FocusedError('Selected working source revision/contents changed during the tests.')
    if any(step.get('status') not in ('passed', 'failed', 'blocked', 'pending') for step in selected):
        raise FocusedError('Selected gates contain an unrecognized status.')
    states = [s['status'] for s in selected]
    # Keep only selected, policy-validated worker evidence in the C1 summary.
    gate_details = [dict(id=s['id'], status=s['status'],
                         exitCode=s.get('exitCode'), durationMs=s.get('durationMs'),
                         testResults=s.get('testResults'), log=s.get('log'),
                         reason=s.get('reason')) for s in selected]
    details = {'gateDetails': gate_details}
    if system_exit == 0 and report.get('checksVerdict') == 'passed' and all(s == 'passed' for s in states) and all(s.get('exitCode') == 0 for s in selected):
        # This is NEVER production CI. The worker validates actual test coverage.
        return 'development_pass', dict(details, selectedCount=len(selected), failedCount=0,
                                        blockedCount=0, selectedSteps=[s['id'] for s in selected])
    if 'failed' in states or report.get('checksVerdict') == 'failed':
        return 'development_failed', dict(details, selectedCount=len(selected), failedCount=states.count('failed'),
                                          blockedCount=states.count('blocked'),
                                          failedSteps=[s['id'] for s in selected if s['status'] != 'passed'])
    return 'development_blocked', dict(details, selectedCount=len(selected), failedCount=states.count('failed'),
                                       blockedCount=states.count('blocked'),
                                       failedSteps=[s['id'] for s in selected if s['status'] != 'passed'])


# These additional offline regressions are not part of the existing Quick policy.
# They are required when modifying the orchestration that invokes that policy.
PYTHON_CHECKS = {
    'c1-python-regressions': ('scripts/dev', 'test_focused.py'),
    'validation-workflow-regressions': ('scripts/validation', 'test_workflow.py'),
    'validation-cross-revision-regressions': ('scripts/validation', 'test_cross_revision.py'),
}
# Reviewed minimum regression coverage at the C1 checkpoint. Additions are
# welcome; a reduction requires explicit source review, never silent acceptance.
PYTHON_TEST_MINIMUMS = {
    'c1-python-regressions': 40,
    'validation-workflow-regressions': 22,
    'validation-cross-revision-regressions': 9,
}


def run_python_check(system: System, root: Path, out: Path, name: str) -> dict:
    directory, pattern = PYTHON_CHECKS[name]
    argv = [sys.executable, '-m', 'unittest', 'discover', '-s', directory, '-p', pattern, '-v']
    logfile = out / (name + '.log')
    code = system.invoke(argv, root, logfile)
    try:
        output = logfile.read_text(encoding='utf-8', errors='replace')
    except OSError as exc:
        raise FocusedError(f'{name}: missing log') from exc
    match = re.search(r'(?m)^Ran (\d+) tests? in ', output)
    observed = int(match.group(1)) if match else 0
    minimum = PYTHON_TEST_MINIMUMS[name]
    complete = bool(observed >= minimum and
                    re.search(r'(?m)^OK\s*$', output) and not
                    re.search(r'(?m)^(?:FAILED|OK \(skipped=)', output))
    state = 'passed' if code == 0 and complete else 'failed' if code else 'blocked'
    return dict(id=name, command=argv, log=str(logfile), exitCode=code,
                status=state, observedCount=observed, minimumExpected=minimum,
                passed=observed if complete and code == 0 else 0)


def render_markdown(report: dict) -> str:
    """Concise human review of the *same* result as summary.json, not a verdict engine.

    Never infer a pass from an exit code alone or from unvalidated worker logs.
    Full evidence remains in the machine-readable summary and worker reports.
    """
    out = Path(report.get('evidenceDirectory', '.'))

    def cell(value: object) -> str:
        return str(value if value is not None else '-').replace('|', r'\|').replace('\n', ' ').replace('`', "'")

    def log_ref(raw: object) -> str:
        if not isinstance(raw, str) or not raw:
            return '-'
        try:
            relative = Path(raw).relative_to(out).as_posix()
        except ValueError:
            return '`' + cell(raw) + '`'
        return '[' + cell(Path(relative).name) + '](' + relative.replace(' ', '%20') + ')'

    status = report.get('status', 'unknown')
    lines = [
        '# CELab development validation', '',
        f'**Verdict: `{cell(status)}`**  |  Exit: `{cell(report.get("exitCode"))}`',
        '',
        '**Windows Quick development scope only. NOT production qualification.**',
        'Approach B Full CI remains a separate, clean-commit acceptance gate.', '',
        '## At a glance', '',
        '| Check | Result |', '| --- | --- |',
        f'| Windows areas | {cell(", ".join(report.get("windowsAreas", [])) or "none")} |',
        f'| Automatic selection | {"No - explicit area override" if report.get("explicitAreaOverride") else "Yes"} |',
        f'| Selected Windows gates | {cell(report.get("selectedCount"))} |',
        f'| Failed / blocked gates and supplemental checks | {cell(report.get("failedCount"))} / {cell(report.get("blockedCount"))} |',
        f'| Windows worker exit | {cell(report.get("workerExit"))} |',
        f'| Pending coverage items | {len(report.get("coveragePending", []))} |',
        f'| Full CI runs / automatic retries / runner model calls | '
        f'{cell(report.get("fullCiInvocations"))} / {cell(report.get("retryCount"))} / {cell(report.get("codexModelCalls"))} |',
        '',
        '## Windows Quick gates', '',
    ]
    gates = report.get('gateDetails', [])
    if gates:
        lines += ['| Gate | Status | Tests passed | Time | Log |', '| --- | --- | ---: | ---: | --- |']
        for g in gates:
            tests = g.get('testResults') or {}
            count = (f'{tests["passed"]}/{tests["total"]}' if isinstance(tests, dict) and
                     isinstance(tests.get('passed'), int) and isinstance(tests.get('total'), int)
                     else '-')
            elapsed = (f'{g["durationMs"] / 1000:.2f}s' if isinstance(g.get('durationMs'), (int, float))
                       and not isinstance(g['durationMs'], bool) else '-')
            lines.append(f'| {cell(g.get("id"))} | {cell(g.get("status"))} | {count} | '
                         f'{elapsed} | {log_ref(g.get("log"))} |')
    else:
        lines.append('No validated Windows gate details available. See the recorded verdict and coordinator log.')
    lines += ['', '## Supplemental Python regressions', '']
    checks = report.get('pythonChecks', [])
    if checks:
        lines += ['| Suite | Status | Tests passed | Minimum | Log |', '| --- | --- | ---: | ---: | --- |']
        for check in checks:
            lines.append(f'| {cell(check.get("id"))} | {cell(check.get("status"))} | '
                         f'{cell(check.get("passed"))} | {cell(check.get("minimumExpected"))} | '
                         f'{log_ref(check.get("log"))} |')
    else:
        planned = report.get('pythonChecksSelected', [])
        lines.append('No supplemental suite results recorded.' +
                     (f' Expected: {cell(", ".join(planned))}.' if planned else ''))
    lines += ['', '## Coverage still pending', '']
    pending = report.get('coveragePending', [])
    if pending:
        lines += [f'- `{cell(item)}`' for item in pending]
        lines.append('')
        lines.append('A partial pass is not full coverage. Review these items separately.')
    else:
        lines.append('None identified **within the selected C1 scope**. This does not imply Linux or Full CI validation.')
    if report.get('reason'):
        lines += ['', '## Blocker / invalid evidence', '', cell(report['reason'])]
    failed = [g for g in gates if g.get('status') != 'passed']
    failed += [c for c in checks if c.get('status') != 'passed']
    if failed:
        lines += ['', '## Failures to investigate', '']
        for item in failed:
            lines.append(f'- **{cell(item.get("id"))}**: {cell(item.get("status"))}; '
                         f'log: {log_ref(item.get("log"))}; {cell(item.get("reason"))}')
    lines += ['', '## Source and evidence', '',
              f'- Tested HEAD: `{cell(report.get("sourceCommit"))}`',
              f'- Selection base: `{cell(report.get("baseRef"))}` (`{cell(report.get("baseCommit"))}`)',
              f'- Changed paths: {len(report.get("changedPaths", []))}',
              '- [Machine-readable C1 summary](summary.json)',
              '- [Windows worker report](windows/report.json)',
              '- [Windows coordinator log](coordinator.log)',
              '',
              'Development-only result; no automatic production acceptance, Git writes, or baseline promotion.', '']
    return '\n'.join(lines)


def run(root: Path, base_ref: str = 'HEAD', explicit_areas: tuple[str, ...] | None = None,
        execute: bool = False, system: System | None = None) -> tuple[int, dict]:
    system = system or System()
    root = resolve_root(system, root.resolve())
    sha, paths, base, tree = collect_paths(system, root, base_ref)
    selection = select(set(paths), explicit_areas)
    initial_digest = source_digest(system, root) if execute else None
    report = {'schemaVersion': 1, 'kind': 'celab-focused-development-c1', 'authoritative': False,
              'productionQualified': False, 'scope': 'windows-only', 'baseRef': base_ref,
              'baseCommit': base, 'sourceCommit': sha, 'sourceTree': tree,
              'changedPaths': list(selection.changed), 'windowsAreas': list(selection.areas),
              'pythonChecksSelected': list(selection.python_checks),
              'linuxPending': list(selection.linux_pending), 'coveragePending': list(selection.linux_pending),
              'explicitAreaOverride': selection.explicit,
              'executionEnabled': execute, 'fullCiInvocations': 0,
              'codexModelCalls': 0, 'retryCount': 0, 'status': 'planned' if not execute else 'running'}
    if not execute:
        return 0, report
    if not system.is_windows():
        raise FocusedError('C1 execution is Windows-only; planning and offline tests are portable.')
    # Pull the exact Quick step inventory from the existing suite.mjs policy.
    # A partial or fabricated worker report must never be accepted.
    try:
        policy = json.loads(system.run(['node', str(root / 'scripts/dev/policy-plan.mjs'),
                                        ','.join(selection.areas)], cwd=root).stdout)
        if not isinstance(policy, list) or not policy:
            raise ValueError('No selected gates in policy')
        if any(not isinstance(s, dict) or not isinstance(s.get('id'), str) or
               type(s.get('selected')) is not bool for s in policy):
            raise ValueError('Invalid CI policy gate')
        if not any(s['selected'] for s in policy):
            raise ValueError('No selected gates in policy')
    except (ValueError, TypeError) as exc:
        raise FocusedError('Unable to obtain the authoritative Quick inventory.') from exc
    # Never write results in a tracked or nonignored directory.
    out_parent = root / 'work' / 'dev-validation'
    probe = out_parent / 'c1-ignore-probe'
    if system.run(['git', 'check-ignore', '-q', str(probe)], cwd=root, checked=False).returncode:
        raise FocusedError('Output is not Git ignored; refusing to write validation evidence.')
    identifier = dt.datetime.now(dt.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ') + '-' + uuid.uuid4().hex[:8]
    out = out_parent / identifier
    out.mkdir(parents=True, exist_ok=False)
    worker = out / 'windows' / 'report.json'
    worker.parent.mkdir(parents=True)
    argv = ['node', str(root / 'scripts/ci/suite.mjs'), 'run', '--repo', str(root), '--platform', 'windows',
            '--report', str(worker), '--mode', 'quick', '--areas', ','.join(selection.areas)]
    report.update(evidenceDirectory=str(out), workerReport=str(worker), invokedCommand=argv,
                  expectedPolicySteps=policy)
    (out / 'plan.json').write_text(json.dumps(report, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    print('C1 Windows Quick worker; detailed output: ' + str(out / 'coordinator.log'), flush=True)
    worker_exit = system.invoke(argv, root, out / 'coordinator.log')
    report['workerExit'] = worker_exit
    try:
        status, summary = read_report(worker, worker_exit, sha, policy)
        report.update(summary)
        # Run additional local Python regressions when affected. Fail closed
        # on missing/malformed unittest output; retain their individual logs.
        extra = [run_python_check(system, root, out, name) for name in selection.python_checks]
        report['pythonChecks'] = extra
        if any(check['status'] != 'passed' for check in extra):
            status = 'development_failed' if any(check['status'] == 'failed' for check in extra) else 'development_blocked'
            report['failedCount'] += sum(check['status'] == 'failed' for check in extra)
            report['blockedCount'] += sum(check['status'] == 'blocked' for check in extra)
        end_sha, end_paths, _, end_tree = collect_paths(system, root, base_ref)
        if (end_sha != sha or end_tree != tree or end_paths != paths or
                source_digest(system, root) != initial_digest):
            raise FocusedError('Git source revision or working source changed during the C1 run.')
        report['status'] = ('development_partial_pass' if selection.linux_pending else status) if status == 'development_pass' else status
        exit_code = {'development_pass': 0, 'development_failed': 1, 'development_blocked': 2}[status]
    except FocusedError as exc:
        report['status'] = 'evidence_invalid'
        report['reason'] = str(exc)
        exit_code = 2
    report['exitCode'] = exit_code
    (out / 'summary.json').write_text(json.dumps(report, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    (out / 'summary.md').write_text(render_markdown(report), encoding='utf-8')
    print('C1 review summary: ' + str(out / 'summary.md'), flush=True)
    return exit_code, report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--base-ref', default='HEAD', help='Local Git ancestor used for change selection, default HEAD')
    parser.add_argument('--areas', help='Explicit comma-separated Windows Quick areas (overrides inferred selection)')
    parser.add_argument('--execute', action='store_true', help='Explicitly run the Windows Quick worker once')
    args = parser.parse_args(argv)
    explicit = tuple(x.strip() for x in args.areas.split(',')) if args.areas is not None else None
    try:
        code, result = run(args.repo, args.base_ref, explicit, args.execute)
        print(json.dumps(result, indent=2, sort_keys=True))
        return code
    except (FocusedError, OSError, ValueError) as exc:
        print('C1 development validation stopped: ' + str(exc), file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
