"""Local deterministic Full CI wrapper. Planning is the default; --execute-full is explicit."""
import argparse
import base64
import datetime as dt
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

from analyze import Analyzer, publish
from evidence import (EvidenceError, bridge, command, digest, git, manifest,
                      read_json, records_hash, stable_records, write_json)


def now():
    return dt.datetime.now(dt.timezone.utc).isoformat()


def coordinator_command(options):
    return ['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
            'scripts/ci/run-all.ps1', '-Mode', 'Full', '-LinuxRepository', options['linux_repo'],
            '-WslDistribution', options['distribution'], '-LinuxNodeVersion', options['linux_node'],
            '-EmsdkRoot', options['emsdk_root']]


def clean_identity(source, commit, tree, actual_tree):
    if source.get('commit') != commit or actual_tree != tree or any(
            source.get(key) != [] for key in ['dirtyTracked', 'untracked', 'indexFlags']):
        raise EvidenceError('Validation checkout must have the exact commit/tree and clean unflagged source')


class LocalSystem:
    def wsl(self, options, args):
        return command(['wsl.exe', '-d', options['distribution'], '--exec', *args], timeout=60)

    def preflight(self, options, output):
        if sys.platform != 'win32':
            raise EvidenceError('The established coordinator requires Windows; use Windows PowerShell')
        repo = Path(options['windows_repo']).resolve()
        if not options['linux_repo'].startswith('/') or '\n' in options['linux_repo']:
            raise EvidenceError('An absolute WSL checkout is required')
        for name in ['expected_commit', 'expected_tree']:
            if not re.fullmatch('[0-9a-f]{40}', options[name]):
                raise EvidenceError('Full Git object IDs are required')
        for program in ['powershell.exe', 'wsl.exe', options['node'], 'git', 'cmake', 'ninja']:
            if not shutil.which(program):
                raise EvidenceError('Missing program: ' + program)
        if not command([options['node'], '--version'], timeout=15).startswith('v24.'):
            raise EvidenceError('Windows application Node 24 required')
        node_path = Path(shutil.which(options['node'])).resolve()
        if not (node_path.parent / 'node_modules/npm/bin/npm-cli.js').is_file():
            raise EvidenceError('npm must be installed beside Windows Node')
        if not (shutil.which('cl.exe') and os.environ.get('VSCMD_ARG_TGT_ARCH') == 'x64'):
            vswhere = Path(os.environ.get('ProgramFiles(x86)', '')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
            installation = command([vswhere, '-latest', '-products', '*', '-requires',
                                    'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
                                    '-property', 'installationPath'], timeout=15)
            if not (Path(installation) / 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll').is_file():
                raise EvidenceError('MSVC x64 initialization unavailable')
        # Validate policy content against Git before importing it, and the reference before running.
        if git(repo, 'hash-object', '--path=scripts/ci/suite.mjs', 'scripts/ci/suite.mjs') != git(
                repo, 'rev-parse', options['expected_commit'] + ':scripts/ci/suite.mjs'):
            raise EvidenceError('Modified CI policy')
        snapshot = bridge(repo, 'inspect', options['node'], target=str(repo))
        clean_identity(snapshot, options['expected_commit'], options['expected_tree'], git(repo, 'rev-parse', 'HEAD^{tree}'))
        reference = Path(options['reference_root'])
        reference_report = read_json(reference / 'full-ci-summary.json')['rawReport']
        reference_commit = read_json(reference / 'fingerprints.json')['frozenCommit']
        # Reference predates A2's normalized sidecar; require its original evidence instead.
        reference_check = Analyzer(repo, options['expected_commit'], options['expected_tree'], options['node'])
        reference_check.contract = bridge(repo, 'contract', options['node'])
        bundle = reference_check.verify_bundle(reference, Path(reference_report).parent, reference_commit, 'reference', False)
        reference_check.integrity(reference, reference_check.consumed.copy())
        if not bundle or reference_check.issues:
            raise EvidenceError('Approved reference evidence is unavailable or inconsistent')
        report_base = repo / 'work/ci'
        git(repo, 'check-ignore', '--', str(report_base / 'preflight-probe'))
        linux_worker = self.wsl(options, ['wslpath', '-a', '-u', str(repo / 'scripts/ci/run-linux.sh')])
        linux_output = self.wsl(options, ['wslpath', '-a', '-u', str(output / 'linux-preflight.json')])
        self.wsl(options, ['bash', '--noprofile', '--norc', linux_worker, '--action', 'inspect',
                           '--repo', options['linux_repo'], '--report', linux_output,
                           '--node-version', options['linux_node'], '--emsdk', options['emsdk_root']])
        linux = read_json(output / 'linux-preflight.json')
        linux_tree = self.wsl(options, ['git', '-C', options['linux_repo'], 'rev-parse', 'HEAD^{tree}'])
        clean_identity(linux, options['expected_commit'], options['expected_tree'], linux_tree)
        for program in ['cmake', 'ninja', 'g++', 'python3']:
            self.wsl(options, [program, '--version'])
        sdk = self.wsl(options, ['bash', '--noprofile', '--norc', linux_worker,
                                '--emsdk', options['emsdk_root'], '--sdk-exec', 'emcc', '--version'])
        return dict(windows=snapshot, linux=linux, windowsTree=options['expected_tree'], linuxTree=linux_tree,
                    sdk=sdk, reportBase=str(report_base), reportDirectoriesBefore=sorted(
                        str(p.resolve()) for p in report_base.iterdir() if p.is_dir()) if report_base.exists() else [])

    def invoke(self, args, cwd, log):
        with Path(log).open('wb') as stream:
            process = subprocess.Popen(args, cwd=cwd, stdout=stream, stderr=subprocess.STDOUT,
                                       creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            try:
                return process.wait()
            except KeyboardInterrupt:
                # Terminate only this owned coordinator process tree, never unrelated workers.
                subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'], capture_output=True, timeout=15)
                process.wait(timeout=15)
                return 130

    def locate(self, preflight):
        existing = set(preflight['reportDirectoriesBefore'])
        created = [p for p in Path(preflight['reportBase']).iterdir() if p.is_dir() and str(p.resolve()) not in existing]
        if len(created) != 1:
            raise EvidenceError('Expected exactly one new original coordinator report directory')
        return created[0]

    def capture(self, options, raw, output, attempt):
        """A2 filenames and schemas; preserve original reports and independently derive sidecars."""
        repo = Path(options['windows_repo'])
        copied = output / 'raw-ci'
        shutil.copytree(raw, copied)
        report = read_json(copied / 'report.json')
        platforms = report.get('platforms', [])
        summary = dict(attempt, rawReport=str(raw / 'report.json'), packagedReport=str(copied / 'report.json'),
                       frozenCommit=options['expected_commit'], tree=options['expected_tree'], attempts=1, reruns=0,
                       verdict=report.get('verdict'), authoritative=report.get('authoritative'),
                       workerExitEvidence=report.get('workerExitEvidence'), steps=report.get('steps', []))
        from collections import Counter
        summary.update(gateCounts=dict(Counter(s.get('status') for s in report.get('steps', []))),
                       platforms=platforms, harnessDurationMs=report.get('durationMs'),
                       testResults=[dict(gate=s['id'], **s['testResults']) for s in report.get('steps', [])
                                    if s.get('testResults')])
        write_json(output / 'full-ci-summary.json', summary)
        files = {}
        selected = git(repo, 'ls-files', '--', 'CMakeLists.txt', 'CMakePresets.json', 'engine/include',
                       'engine/src/evaluation', 'engine/src/chess_core.cpp', 'engine/src/native_move_batch.hpp',
                       'engine/src/search', 'tests/search', 'scripts/ci', 'controller/package.json',
                       'controller/package-lock.json', 'gui/package.json', 'gui/package-lock.json',
                       'gui/playwright.config.ts').splitlines()
        for name in selected:
            files[name] = dict(gitBlob=git(repo, 'rev-parse', options['expected_commit'] + ':' + name),
                               sha256WindowsBytes=digest(repo / name))
        windows_artifacts = {}
        for preset in ['native-debug', 'native-release', 'native-profile']:
            directory = repo / 'build' / preset
            for path in directory.iterdir() if directory.exists() else []:
                if path.is_file() and (path.suffix in ['.exe', '.dll'] or path.name == 'CMakeCache.txt'):
                    windows_artifacts[path.relative_to(repo).as_posix()] = dict(sha256=digest(path), bytes=path.stat().st_size)
            for name in ['LastTest.log', 'LastTestsFailed.log', 'CTestCostData.txt']:
                path = directory / 'Testing/Temporary' / name
                if path.exists():
                    target = output / 'full-extra/windows' / preset / name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(path, target)
        # One read-only WSL probe; no shell interpolation, builds, tests, or benchmarks.
        linux_script = '''import pathlib,json,hashlib,base64,sys
p=pathlib.Path(sys.argv[1]);files={};artifacts={}
for preset in ['native-debug','native-release','native-profile','wasm-debug']:
 d=p/'build'/preset
 for q in d.iterdir() if d.exists() else []:
  if q.is_file() and (q.name=='CMakeCache.txt' or q.suffix in ['.so','.wasm','.js'] or (q.stat().st_mode&0o111 and q.name.startswith(('chess','m')))):
   artifacts[q.relative_to(p).as_posix()]={'sha256':hashlib.sha256(q.read_bytes()).hexdigest(),'bytes':q.stat().st_size}
 for name in ['LastTest.log','LastTestsFailed.log','CTestCostData.txt']:
  q=d/'Testing/Temporary'/name
  if q.exists():files[preset+'/'+name]=base64.b64encode(q.read_bytes()).decode()
print(json.dumps({'files':files,'artifacts':artifacts}))
'''
        linux = json.loads(self.wsl(options, ['python3', '-c', linux_script, options['linux_repo']]))
        for name, data in linux['files'].items():
            target = output / 'full-extra/linux' / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(base64.b64decode(data))
        fingerprints = dict(frozenCommit=options['expected_commit'], tree=options['expected_tree'],
                            sourceEvaluatorConfigurationFiles=files, windowsBuildArtifacts=windows_artifacts,
                            linuxBuildArtifacts=linux['artifacts'],
                            sourceIdentities={p['platform']: {k: p.get(k) for k in ['sourceBefore', 'sourceAfter', 'sourceFinal']}
                                              for p in platforms},
                            platforms={p['platform']: {k: p.get(k) for k in ['host', 'toolchains', 'harness', 'workflows']}
                                       for p in platforms})
        write_json(output / 'fingerprints.json', fingerprints)
        records = []
        for path in sorted((output / 'full-extra').glob('*/*/LastTest.log')):
            platform, preset = path.relative_to(output / 'full-extra').parts[:2]
            values = stable_records(path)
            normalized = path.with_name('deterministic-records.txt')
            normalized.write_text('\n'.join(values) + '\n', encoding='utf-8', newline='\n')
            records.append(dict(platform=platform, preset=preset, recordCount=len(values),
                                normalizedSha256=records_hash(values), recordsPath=normalized.relative_to(output).as_posix(),
                                rawLog=dict(path=path.relative_to(output).as_posix(), sha256=digest(path), bytes=path.stat().st_size)))
        write_json(output / 'deterministic-test-fingerprints.json', dict(records=records,
                   rules='A2 printed-record extraction; elapsed seconds excluded; normalized text is UTF-8 LF.'))
        write_json(output / 'evidence-manifest.json', manifest(output))
        return copied


def allowance(options, output, label):
    collector, cli = options.get('telemetry_collector'), options.get('rate_cli')
    if not collector and not cli:
        value = dict(available=False, reason='No allowance collector configured; token usage is not inferred.')
    elif not collector or not cli:
        raise EvidenceError('--telemetry-collector and --rate-cli must be supplied together')
    else:
        spec = importlib.util.spec_from_file_location('existing_telemetry', collector)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        value = module.rate_snapshot(cli)
    write_json(output / ('allowance-' + label + '.json'), value)


def run(options, execute=False, system=None):
    system = system or LocalSystem()
    output = Path(options['out']).resolve()
    # Exclusive creation prevents clobbering evidence or reusing an invocation record.
    output.mkdir(parents=True, exist_ok=False)
    args = coordinator_command(options)
    record = dict(schemaVersion=1, command=args, cwd=str(Path(options['windows_repo']).resolve()),
                  startedUTC=now(), finishedUTC=None, fullCiInvocations=0, retries=0, interventions=[],
                  executionEnabled=execute, modelUsage=dict(execution='explicitly_model_free', modelCalls=0,
                  implementationTokens=None, routineTokens=None, anomalyInvestigationTokens=None))
    write_json(output / 'run.json', record)
    if not execute:
        record.update(finishedUTC=now(), status='planned_execution_disabled')
        write_json(output / 'run.json', record)
        (output / 'report.md').write_text('Planning only. Full CI execution is disabled; zero coordinator invocations.\n', encoding='utf-8')
        write_json(output / 'evidence-manifest.json', manifest(output))
        return record
    attempt = None
    try:
        allowance(options, output, 'before')
        preflight = system.preflight(options, output)
        write_json(output / 'preflight.json', preflight)
        attempt = dict(command=args, cwd=record['cwd'], startedUTC=now(), finishedUTC=None,
                       durationSeconds=None, exitCode=None, invocation=1,
                       reportLocation='coordinator default work/ci; no ReportRoot override')
        record['fullCiInvocations'] = 1
        write_json(output / 'run.json', record)
        write_json(output / 'full-attempt.json', attempt)
        started = time.perf_counter()
        try:
            attempt['exitCode'] = system.invoke(args, record['cwd'], output / 'full-console.log')
        finally:
            attempt.update(finishedUTC=now(), durationSeconds=time.perf_counter() - started)
            write_json(output / 'full-attempt.json', attempt)
        allowance(options, output, 'after')
        raw = system.locate(preflight)
        record['originalReportDirectory'] = str(raw)
        copied = system.capture(options, raw, output, attempt)
        analyzer = Analyzer(options['windows_repo'], options['expected_commit'], options['expected_tree'], options['node'])
        from analyze import resolve_report
        verdict = analyzer.run(copied, output, options['reference_root'], resolve_report(options['reference_root']))
        if attempt['exitCode'] != 0:
            verdict['classification'], verdict['authoritative'] = 'validation_failed', False
            verdict['discrepancies'].append(dict(classification='validation_failed', code='coordinator_process_exit',
                                                path=str(raw), expected=0, actual=attempt['exitCode']))
        publish(verdict, output / 'analysis')
        record.update(status='completed', classification=verdict['classification'], authoritative=verdict['authoritative'])
    except (EvidenceError, OSError, ValueError, subprocess.SubprocessError, KeyError, TypeError) as error:
        record.update(status='stopped', authoritative=False,
                      classification='validation_failed' if attempt and attempt.get('exitCode') not in [None, 0]
                      else 'evidence_incomplete', error=str(error))
    finally:
        record['finishedUTC'] = now()
        write_json(output / 'run.json', record)
        (output / 'report.md').write_text('\n\n'.join([
            '# Local validation wrapper', '**' + record.get('classification', 'evidence_incomplete') + '**',
            'Full invocations: ' + str(record['fullCiInvocations']) + '; automatic retries: 0.',
            'Original reports: ' + record.get('originalReportDirectory', 'not produced'),
            'Error: ' + record.get('error', 'none'),
            'See run.json, full-attempt.json when present, and analysis/ for detailed evidence.',
            'Execution was explicitly model-free. Implementation and anomaly-investigation usage are separate.'
        ]) + '\n', encoding='utf-8')
        write_json(output / 'evidence-manifest.json', manifest(output))
    return record


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ['windows-repo', 'linux-repo', 'expected-commit', 'expected-tree', 'reference-root', 'out', 'emsdk-root']:
        p.add_argument('--' + name, required=True)
    p.add_argument('--distribution', default='Ubuntu-24.04')
    p.add_argument('--linux-node', default='24.10.0')
    p.add_argument('--node', default='node')
    p.add_argument('--telemetry-collector')
    p.add_argument('--rate-cli')
    p.add_argument('--execute-full', action='store_true', help='Explicitly enable exactly one real Full invocation')
    return p


if __name__ == '__main__':
    options = vars(parser().parse_args())
    enabled = options.pop('execute_full')
    try:
        result = run(options, enabled)
    except (OSError, EvidenceError) as error:
        print(json.dumps(dict(status='stopped', error=str(error))))
        raise SystemExit(1)
    print(json.dumps(result, sort_keys=True))
    raise SystemExit(0 if result.get('authoritative') or not enabled else 1)
