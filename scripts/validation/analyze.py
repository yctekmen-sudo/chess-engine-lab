"""Offline validation evidence analyzer. No CI, model calls, or source writes."""
import argparse
from collections import Counter
import copy
import json
from pathlib import Path
import re
import subprocess

from evidence import (EvidenceError, bridge, contained, digest, git, read_json,
                      records_hash, runtime_tools, stable_records, write_json)


class Analyzer:
    def __init__(self, source_repo, expected_commit, expected_tree, node='node'):
        self.repo = Path(source_repo).resolve()
        self.commit, self.tree, self.node = expected_commit, expected_tree, node
        self.issues, self.comparisons, self.consumed = [], [], set()
        self.contract = None

    def issue(self, kind, code, path, expected=None, actual=None):
        self.issues.append(dict(classification=kind, code=code, path=str(path),
                                expected=expected, actual=actual))

    def require(self, condition, code, path, expected=None, actual=None,
                kind='evidence_incomplete'):
        if not condition:
            self.issue(kind, code, path, expected, actual)
        return condition

    def read(self, path):
        try:
            result = read_json(path)
            self.consumed.add(Path(path).resolve())
            return result
        except EvidenceError as error:
            self.issue('evidence_incomplete', 'unreadable_json', path, actual=str(error))
            return None

    def text(self, path):
        try:
            result = Path(path).read_text(encoding='utf-8-sig')
            self.consumed.add(Path(path).resolve())
            return result
        except (OSError, UnicodeError) as error:
            self.issue('evidence_incomplete', 'unreadable_log', path, actual=str(error))
            return ''

    def equal(self, actual, expected, code, path, kind='fingerprint_mismatch'):
        return self.require(actual == expected, code, path, expected, actual, kind)

    def inventory(self, steps, expected, label):
        self.require(isinstance(steps, list), 'invalid_gate_array', label)
        steps = steps if isinstance(steps, list) else []
        ids = [s.get('id') if isinstance(s, dict) else None for s in steps]
        counts = Counter(str(i) for i in ids)
        self.require(len(ids) == len(expected) and set(ids) == set(expected),
                     'gate_inventory', label, expected, ids)
        self.require(all(v == 1 for v in counts.values()), 'duplicate_gates', label,
                     actual={k: v for k, v in counts.items() if v != 1})
        return [s for s in steps if isinstance(s, dict)]

    def verify_tree(self, fingerprints, expected_commit, label, candidate):
        # Reference and candidate revisions can legitimately have distinct Git
        # trees. Reject corruption of either recorded tree and pin the selected
        # candidate to its explicitly supplied expected tree.
        if candidate:
            self.equal(fingerprints.get('tree'), self.tree, 'git_tree', label)
        try:
            recorded_tree = git(self.repo, 'rev-parse', expected_commit + '^{tree}')
            self.equal(fingerprints.get('tree'), recorded_tree, 'git_object_tree', label)
        except subprocess.SubprocessError as error:
            self.issue('evidence_incomplete', 'git_object_unavailable', label, actual=str(error))

    def verify_bundle(self, root, report_dir, expected_commit, label, candidate):
        root, report_dir = Path(root).resolve(), Path(report_dir).resolve()
        self.require(report_dir.is_relative_to(root), 'unsealed_external_report', report_dir,
                     'report directory inside its sealed evidence package')
        report = self.read(report_dir / 'report.json')
        # A1 has only a packaging summary. It is never a coordinator report.
        if not isinstance(report, dict):
            attempt = self.read(root / 'full-attempt.json')
            if isinstance(attempt, dict) and type(attempt.get('exitCode')) is int and attempt['exitCode'] != 0:
                self.issue('validation_failed', 'coordinator_exit', label, 0, attempt['exitCode'])
            return None
        self.equal(report.get('schemaVersion'), 1, 'report_schema', label, 'evidence_incomplete')
        invocations = self.read(report_dir / 'coordinator.json')
        if self.require(isinstance(invocations, list) and bool(invocations), 'missing_coordinator_invocations', label):
            for index, invocation in enumerate(invocations):
                if not self.require(isinstance(invocation, dict), 'invalid_coordinator_invocation', label):
                    continue
                code = invocation.get('exitCode')
                self.require(type(code) is int and code == 0 and invocation.get('status') == 'passed',
                             'coordinator_invocation_exit', label + '/' + str(index), 'passed/0',
                             [invocation.get('status'), code],
                             'validation_failed' if type(code) is int and code != 0 else 'evidence_incomplete')
                path = report_dir / ('coordinator-' + str(index) + '.log')
                if self.require(path.is_file(), 'missing_coordinator_log', path):
                    self.consumed.add(path.resolve())
            self.require(isinstance(invocations[-1], dict) and 'combine' in invocations[-1].get('command', []),
                         'missing_coordinator_combine', label)
        # A standalone original CI report need not have an external timing wrapper.
        # When a wrapper exit is recorded, it cannot be overridden by a passing JSON report.
        attempt = self.read(root / 'full-attempt.json') if (root / 'full-attempt.json').exists() else None
        if isinstance(attempt, dict):
            code = attempt.get('exitCode')
            self.require(type(code) is int and code == 0, 'coordinator_process_exit', label, 0, code,
                         'validation_failed' if type(code) is int and code != 0 else 'evidence_incomplete')
        self.require(report.get('mode') == 'full' and report.get('authoritative') is True and
                     report.get('verdict') == 'passed', 'coordinator_not_authoritative', label,
                     'full/passed/true', [report.get(k) for k in ['mode', 'verdict', 'authoritative']],
                     'non_authoritative')
        self.equal(report.get('commit'), expected_commit, 'source_commit', label)
        exits = self.read(report_dir / 'worker-exits.json')
        if isinstance(exits, dict):
            self.equal(exits.get('schemaVersion'), 1, 'exit_schema', label, 'evidence_incomplete')
            value = exits.get('exits')
            self.require(isinstance(value, dict) and set(value) == {'windows', 'linux'} and
                         all(type(v) is int for v in value.values()), 'worker_exit_inventory', label)
            if isinstance(value, dict):
                for platform, code in value.items():
                    self.equal(code, 0, 'worker_exit', label + '/' + platform,
                               'validation_failed' if type(code) is int and code != 0 else 'evidence_incomplete')
            self.equal(report.get('workerExitEvidence'), exits, 'embedded_worker_exits', label,
                       'evidence_incomplete')
        self.equal(report.get('testBaselines'), self.contract['testBaselines'], 'test_floors', label)
        originals, log_requests = [], []
        expected_all = [p + '/' + s['id'] for p, plan in self.contract['plans'].items() for s in plan]
        top_steps = self.inventory(report.get('steps'), expected_all, label + '/combined')
        for step in top_steps:
            if step.get('status') == 'failed' or (type(step.get('exitCode')) is int and step['exitCode'] != 0):
                self.issue('validation_failed', 'failed_gate', label + '/' + str(step.get('id')),
                           'passed/0', [step.get('status'), step.get('exitCode')])
        embedded = report.get('platforms')
        self.require(isinstance(embedded, list) and len(embedded) == 2 and
                     Counter(p.get('platform') for p in embedded if isinstance(p, dict)) ==
                     Counter(['windows', 'linux']), 'platform_inventory', label)
        for platform, plan in self.contract['plans'].items():
            worker = self.read(report_dir / platform / 'report.json')
            initial = self.read(report_dir / (platform + '-source.json'))
            final = self.read(report_dir / (platform + '-final-source.json'))
            if not isinstance(worker, dict):
                continue
            self.equal(worker.get('platform'), platform, 'worker_platform', label)
            self.equal(worker.get('testBaselines'), self.contract['testBaselines'], 'worker_test_floors', label)
            self.require(worker.get('checksVerdict') == 'passed' and worker.get('mode') == 'full',
                         'worker_not_complete', label + '/' + platform, kind='non_authoritative')
            self.equal(worker.get('sourceBefore'), initial, 'initial_source_copy', label, 'evidence_incomplete')
            complete = copy.deepcopy(worker)
            complete['sourceFinal'] = final
            originals.append(complete)
            matches = [p for p in embedded or [] if isinstance(p, dict) and p.get('platform') == platform]
            self.require(len(matches) == 1 and matches[0] == complete, 'worker_report_copy',
                         label + '/' + platform)
            for phase in ['sourceBefore', 'sourceAfter', 'sourceFinal']:
                source = complete.get(phase)
                if not self.require(isinstance(source, dict), 'missing_source_state', label + '/' + phase):
                    continue
                self.equal(source.get('commit'), expected_commit, 'source_commit', label + '/' + phase)
                for field in ['dirtyTracked', 'untracked', 'indexFlags']:
                    self.require(source.get(field) == [], 'unclean_source', label + '/' + phase + '/' + field,
                                 [], source.get(field), 'non_authoritative')
                for field in ['trackedFingerprint', 'indexFingerprint', 'untrackedFingerprint']:
                    self.require(bool(re.fullmatch('[0-9a-f]{64}', str(source.get(field)))),
                                 'missing_source_fingerprint', label + '/' + phase + '/' + field)
            for field in ['trackedFingerprint', 'indexFingerprint', 'untrackedFingerprint']:
                values = [complete.get(phase, {}).get(field) if isinstance(complete.get(phase), dict) else None
                          for phase in ['sourceBefore', 'sourceAfter', 'sourceFinal']]
                self.require(len(set(values)) == 1, 'source_changed', label + '/' + platform + '/' + field,
                             actual=values, kind='non_authoritative')
            steps = self.inventory(worker.get('steps'), [p['id'] for p in plan], label + '/' + platform)
            mapped = {s['id']: s for s in steps if isinstance(s.get('id'), str)}
            for definition in plan:
                gate = definition['id']
                step = mapped.get(gate)
                if step is None:
                    continue
                scope = label + '/' + platform + '/' + gate
                self.require(step.get('status') == 'passed' and step.get('selected') is True and
                             type(step.get('exitCode')) is int and step['exitCode'] == 0,
                             'gate_not_passed', scope, 'passed/selected/0',
                             [step.get(k) for k in ['status', 'selected', 'exitCode']], 'non_authoritative')
                log_path = report_dir / platform / (gate + '.log')
                text = self.text(log_path)
                if definition.get('inventory'):
                    try:
                        inventory = json.loads(text[text.index('{'):])['tests']
                        names = [t['name'] for t in inventory]
                        self.require(names == step.get('discoveredTests') and len(set(names)) == len(names)
                                     and set(definition['inventory']) <= set(names) and
                                     not any(p.get('name') == 'DISABLED' and p.get('value') for t in inventory
                                             for p in t.get('properties', [])), 'ctest_inventory', scope)
                    except (ValueError, KeyError, TypeError):
                        self.issue('evidence_incomplete', 'invalid_ctest_inventory', scope)
                inventory_step = mapped.get(gate.replace('-ctest', '-inventory'), {})
                count = len(inventory_step.get('discoveredTests', []))
                log_requests.append(dict(id=platform + '/' + gate, gate=gate,
                                         text=text[-128 * 1024:], count=count))
            self.equal([s for s in top_steps if str(s.get('id', '')).startswith(platform + '/')],
                       [dict(s, id=platform + '/' + s['id']) for s in steps], 'combined_gate_copy',
                       label + '/' + platform, 'evidence_incomplete')
        if len(originals) == 2 and isinstance(exits, dict):
            try:
                assessment = bridge(self.repo, 'assess', self.node, mode=report.get('mode'),
                                    reports=originals, exits=exits.get('exits'), logs=log_requests)
                self.equal(assessment['verdict'], 'passed', 'existing_ci_verdict', label, 'non_authoritative')
                top = {s['id']: s for s in top_steps}
                for test in assessment['tests']:
                    self.require('error' not in test, 'test_log_incomplete', label + '/' + test['id'],
                                 actual=test.get('error'))
                    if 'results' in test:
                        self.equal(top.get(test['id'], {}).get('testResults'), test['results'],
                                   'test_results_copy', label + '/' + test['id'], 'evidence_incomplete')
            except (ValueError, subprocess.SubprocessError, KeyError, TypeError) as error:
                self.issue('evidence_incomplete', 'ci_policy_rejected_evidence', label, actual=str(error))
        fingerprints = self.read(root / 'fingerprints.json')
        if not isinstance(fingerprints, dict):
            fingerprints = {}
        self.equal(fingerprints.get('frozenCommit'), expected_commit, 'fingerprint_commit', label)
        # Different approved reference revisions must not be mistaken for a
        # candidate's corrupt tree. Cross-revision changes remain review-gated.
        self.verify_tree(fingerprints, expected_commit, label, candidate)
        sources = fingerprints.get('sourceEvaluatorConfigurationFiles')
        if self.require(isinstance(sources, dict) and bool(sources), 'missing_evaluator_source', label):
            for name, value in sources.items():
                try:
                    self.equal(value.get('gitBlob'), git(self.repo, 'rev-parse', expected_commit + ':' + name),
                               'source_blob', label + '/' + name)
                except (subprocess.SubprocessError, AttributeError) as error:
                    self.issue('evidence_incomplete', 'source_blob_unavailable', label + '/' + name, actual=str(error))
        for p in originals:
            platform = p['platform']
            identities = (fingerprints.get('sourceIdentities') or {}).get(platform)
            self.equal(identities, {k: p.get(k) for k in ['sourceBefore', 'sourceAfter', 'sourceFinal']},
                       'source_fingerprint_copy', label + '/' + platform)
            tools = (fingerprints.get('platforms') or {}).get(platform, {})
            self.equal(runtime_tools(tools.get('toolchains', {})), runtime_tools(p.get('toolchains', {})),
                       'toolchain_fingerprint_copy', label + '/' + platform)
            self.equal(tools.get('harness'), p.get('harness'), 'harness_fingerprint_copy', label + '/' + platform)
        derived = {}
        for platform, plan in self.contract['plans'].items():
            for step in plan:
                if not step['id'].endswith('-ctest'):
                    continue
                preset = step['id'].replace('-ctest', '')
                if preset == 'wasm':
                    preset = 'wasm-debug'
                path = root / 'full-extra' / platform / preset / 'LastTest.log'
                self.text(path)
                try:
                    records = stable_records(path)
                    self.require(bool(records), 'missing_deterministic_records', path)
                    derived[(platform, preset)] = dict(records=records, recordCount=len(records),
                                                       normalizedSha256=records_hash(records), rawSha256=digest(path))
                except (OSError, UnicodeError) as error:
                    self.issue('evidence_incomplete', 'unreadable_deterministic_log', path, actual=str(error))
        if candidate:
            declared = self.read(root / 'deterministic-test-fingerprints.json')
            records = declared.get('records') if isinstance(declared, dict) else None
            self.require(isinstance(records, list), 'missing_deterministic_fingerprints', label)
            indexed = {}
            for item in records or []:
                if not isinstance(item, dict):
                    self.issue('evidence_incomplete', 'invalid_deterministic_fingerprint', label)
                    continue
                key = (item.get('platform'), item.get('preset'))
                self.require(key not in indexed, 'duplicate_deterministic_fingerprint', label, actual=key)
                indexed[key] = item
            self.equal(sorted(indexed), sorted(derived), 'deterministic_set_inventory', label, 'evidence_incomplete')
            for key, value in derived.items():
                row = indexed.get(key, {})
                scope = label + '/' + '/'.join(key)
                self.equal(row.get('recordCount'), value['recordCount'], 'deterministic_fingerprint_copy', scope)
                # A2 hashed Windows-written CRLF text, even for Linux records.
                # Check the declared bytes, then compare canonical lines derived from the original log.
                try:
                    record_path = contained(root, row['recordsPath'])
                    record_text = self.text(record_path)
                    self.equal(digest(record_path), row.get('normalizedSha256'), 'normalized_file_hash', scope)
                    self.equal(record_text.splitlines(), value['records'], 'normalized_file_records', scope)
                    self.equal(row.get('rawLog', {}).get('sha256'), value['rawSha256'], 'original_test_log_hash', scope)
                except (EvidenceError, KeyError, OSError) as error:
                    self.issue('evidence_incomplete', 'missing_normalized_file', scope, actual=str(error))
        return dict(root=root, report_dir=report_dir, report=report, fingerprints=fingerprints,
                    deterministic=derived, platforms=originals, attempt=attempt, invocations=invocations)

    def integrity(self, root, consumed):
        root = Path(root).resolve()
        data = self.read(root / 'evidence-manifest.json')
        entries = data.get('files') if isinstance(data, dict) else None
        if not self.require(isinstance(entries, list), 'missing_integrity_inventory', root):
            return
        indexed = {}
        for row in entries:
            try:
                path = contained(root, row['path'])
                self.require(path not in indexed, 'duplicate_integrity_entry', path)
                indexed[path] = row
            except (EvidenceError, TypeError, KeyError) as error:
                self.issue('evidence_incomplete', 'invalid_integrity_entry', root, actual=str(error))
        for path in sorted(consumed):
            if not path.is_relative_to(root) or not path.exists():
                continue
            row = indexed.get(path)
            if not self.require(isinstance(row, dict), 'unsealed_evidence', path):
                continue
            self.require(row.get('bytes') == path.stat().st_size and row.get('sha256') == digest(path),
                         'evidence_hash_mismatch', path, row, dict(bytes=path.stat().st_size, sha256=digest(path)))

    def compare_source_working_fingerprints(self, candidate, reference, platform,
                                           candidate_commit, reference_commit):
        # Integrity of EACH source revision is independently checked in
        # verify_bundle: clean tracked/untracked/index flags, identical
        # before/after/final fingerprints, matching commit and Git tree.
        # Whole-tree fingerprints from DIFFERENT committed revisions are
        # not comparable; they encode deliberate committed source changes.
        fields = ('trackedFingerprint', 'indexFingerprint', 'untrackedFingerprint')
        if candidate_commit == reference_commit:
            for field in fields:
                self.equal(candidate['sourceBefore'].get(field), reference['sourceBefore'].get(field),
                           'source_working_fingerprint', platform + '/' + field)
        else:
            self.comparisons.append(dict(scope='source working fingerprints', platform=platform,
                                         comparison='not_comparable_across_commits',
                                         candidateCommit=candidate_commit, referenceCommit=reference_commit,
                                         fields=list(fields), integrityCheck='verify_bundle'))

    def compare(self, current, approved):
        c, a = current['fingerprints'], approved['fingerprints']
        def actions(rows):
            result = []
            for row in rows or []:
                argv = row.get('command', [])
                if '--action' in argv and argv.index('--action') + 1 < len(argv):
                    result.append(argv[argv.index('--action') + 1])
                else:
                    result.append(next((arg for arg in argv if arg in ['inspect', 'run', 'combine']), None))
            return result
        self.equal(actions(current.get('invocations')), actions(approved.get('invocations')),
                   'coordinator_invocation_inventory', 'comparison', 'evidence_incomplete')
        # Distinguish documentation/validation-tooling changes from candidate
        # implementation changes. The latter always require human review,
        # even when today's deterministic record inventory happens to match.
        if c['frozenCommit'] != a['frozenCommit']:
            try:
                delta = git(self.repo, 'diff', '--name-only', '--no-renames', '-z',
                            a['frozenCommit'], c['frozenCommit'], '--')
                changed_paths = sorted(path for path in delta.split('\0') if path)
                review_paths = [path for path in changed_paths if not (
                    path == 'AGENTS.md' or path.startswith('docs/') or
                    path.startswith('scripts/validation/'))]
                self.comparisons.append(dict(scope='cross-revision Git source delta',
                                             referenceCommit=a['frozenCommit'],
                                             candidateCommit=c['frozenCommit'],
                                             changedPaths=changed_paths,
                                             implementationChangesRequiringReview=review_paths))
                if review_paths:
                    self.issue('fingerprint_mismatch', 'source_revision_requires_review',
                               'comparison/source_delta', expected='Documentation and validation tooling only',
                               actual=review_paths)
            except subprocess.SubprocessError as error:
                self.issue('evidence_incomplete', 'source_delta_unavailable',
                           'comparison/source_delta', actual=str(error))
        current_sources = c.get('sourceEvaluatorConfigurationFiles') or {}
        approved_sources = a.get('sourceEvaluatorConfigurationFiles') or {}
        for name, value in approved_sources.items():
            self.equal(current_sources.get(name), value, 'source_evaluator_configuration', name)
        self.comparisons.append(dict(scope='source inventory', additionalRecordedSources=sorted(
            set(current_sources) - set(approved_sources))))
        for platform in ['windows', 'linux']:
            cp = next(p for p in current['platforms'] if p['platform'] == platform)
            ap = next(p for p in approved['platforms'] if p['platform'] == platform)
            self.equal(runtime_tools(cp.get('toolchains', {})), runtime_tools(ap.get('toolchains', {})),
                       'toolchain_identity', platform)
            self.equal(cp.get('host'), ap.get('host'), 'host_identity', platform)
            self.equal(cp.get('harness'), ap.get('harness'), 'harness_identity', platform)
            for step in ap['steps']:
                if 'discoveredTests' in step:
                    candidate_step = next((s for s in cp['steps'] if s.get('id') == step['id']), {})
                    self.equal(candidate_step.get('discoveredTests'), step['discoveredTests'],
                               'reference_ctest_inventory', platform + '/' + step['id'])
            self.compare_source_working_fingerprints(cp, ap, platform,
                                                     c['frozenCommit'], a['frozenCommit'])
            key = platform + 'BuildArtifacts'
            artifacts, prior = c.get(key), a.get(key)
            self.require(isinstance(artifacts, dict) and bool(artifacts) and isinstance(prior, dict),
                         'missing_build_fingerprints', platform)
            if isinstance(artifacts, dict) and isinstance(prior, dict):
                normalize = lambda rows: {k.replace('\\', '/'): v for k, v in rows.items()}
                self.require(len(normalize(artifacts)) == len(artifacts), 'duplicate_artifact_path', platform)
                artifacts, prior = normalize(artifacts), normalize(prior)
                # A2 recorded top-level build outputs; the premerge package additionally
                # recorded nested policy CMake caches/ordering-policy.txt. These are not binaries.
                binary = lambda name: not name.endswith(('CMakeCache.txt', 'ordering-policy.txt'))
                self.equal(sorted(k for k in artifacts if binary(k)), sorted(k for k in prior if binary(k)),
                           'build_artifact_inventory', platform, 'evidence_incomplete')
                for name, value in artifacts.items():
                    self.require(isinstance(value, dict) and
                                 re.fullmatch('[0-9a-f]{64}', str(value.get('sha256'))) is not None and
                                 type(value.get('bytes')) is int and value['bytes'] > 0,
                                 'invalid_build_fingerprint', platform + '/' + name)
                self.comparisons.append(dict(scope=key, runtimeVariable='binary bytes, debug paths and CMakeCache',
                                             changed=sum(artifacts.get(k) != v for k, v in prior.items()),
                                             extraRecorded=sorted(set(artifacts) - set(prior)),
                                             missingRuntimeConfigurationRecords=sorted(set(prior) - set(artifacts))))
        self.equal(sorted(current['deterministic']), sorted(approved['deterministic']),
                   'reference_deterministic_sets', 'comparison', 'evidence_incomplete')
        for key, value in current['deterministic'].items():
            prior = approved['deterministic'].get(key, {})
            self.equal(value['records'], prior.get('records'), 'deterministic_records', '/'.join(key))
            self.comparisons.append(dict(platform=key[0], preset=key[1], recordCount=value['recordCount'],
                                         normalizedSha256=value['normalizedSha256'],
                                         matchesAcceptedReference=value['records'] == prior.get('records')))

    def run(self, report_dir, evidence_root, reference_root, reference_report):
        current = None
        try:
            if not all(isinstance(v, str) and re.fullmatch('[0-9a-f]{40}', v) for v in [self.commit, self.tree]):
                raise EvidenceError('Expected commit and tree must be full Git object IDs')
            # Loading policy is read-only; refuse policy edits relative to the supplied Git revision.
            actual = git(self.repo, 'hash-object', '--path=scripts/ci/suite.mjs', 'scripts/ci/suite.mjs')
            self.equal(actual, git(self.repo, 'rev-parse', self.commit + ':scripts/ci/suite.mjs'),
                       'existing_policy_modified', self.repo)
            if self.issues:
                raise EvidenceError('The existing policy adapter cannot use a modified policy')
            self.contract = bridge(self.repo, 'contract', self.node)
            reference_fp = self.read(Path(reference_root) / 'fingerprints.json')
            ref_commit = reference_fp.get('frozenCommit') if isinstance(reference_fp, dict) else None
            if not isinstance(ref_commit, str) or not re.fullmatch('[0-9a-f]{40}', ref_commit):
                raise EvidenceError('Reference source commit unavailable')
            approved = self.verify_bundle(reference_root, reference_report, ref_commit, 'reference', False)
            ref_consumed = set(self.consumed)
            self.integrity(reference_root, ref_consumed)
            current = self.verify_bundle(evidence_root, report_dir, self.commit, 'candidate', True)
            self.integrity(evidence_root, self.consumed - ref_consumed)
            if current and approved and len(current['platforms']) == len(approved['platforms']) == 2:
                self.compare(current, approved)
        except (EvidenceError, subprocess.SubprocessError, OSError, ValueError, KeyError, TypeError, AttributeError) as error:
            self.issue('evidence_incomplete', 'invalid_evidence', 'analysis', actual=str(error))
        classes = {i['classification'] for i in self.issues}
        classification = next((k for k in ['validation_failed', 'fingerprint_mismatch', 'evidence_incomplete',
                                           'non_authoritative'] if k in classes), 'authoritative_pass')
        report = current['report'] if current else {}
        return dict(schemaVersion=1, classification=classification,
                    authoritative=classification == 'authoritative_pass', expectedCommit=self.commit,
                    expectedTree=self.tree, reportDirectory=str(Path(report_dir).resolve()),
                    sourceStates={p['platform']: {k: p.get(k) for k in ['sourceBefore', 'sourceAfter', 'sourceFinal']}
                                  for p in current['platforms']} if current else {},
                    gateCounts=dict(Counter(s.get('status') for s in report.get('steps', []))),
                    steps=report.get('steps', []), workerExitEvidence=report.get('workerExitEvidence'),
                    coordinatorVerdict=report.get('verdict'), comparisons=self.comparisons,
                    coordinatorAttempt=current.get('attempt') if current else None,
                    startedAt=report.get('startedAt'), finishedAt=report.get('finishedAt'),
                    durationMs=report.get('durationMs'), discrepancies=sorted(self.issues,
                        key=lambda i: (i['path'], i['code'], json.dumps(i, sort_keys=True))),
                    modelUsage=dict(execution='explicitly_model_free', modelCalls=0,
                                    implementationTokens=None, anomalyInvestigationTokens=None,
                                    note='Implementation/investigation telemetry is separately scoped; missing tokens are not zero.'))


def resolve_report(root):
    root = Path(root).resolve()
    if (root / 'report.json').is_file():
        return root
    roots = list((root / 'windows-ci/work/ci').glob('*/report.json'))
    if len(roots) == 1:
        return roots[0].parent
    raise EvidenceError('Supply --report-dir: no unique original coordinator report')


def analyze(evidence_root, reference_root, source_repo, expected_commit, expected_tree,
            report_dir=None, reference_report=None, node='node'):
    # A1 has no report; keep the missing original path explicit instead of using its synthetic summary.
    try:
        candidate = report_dir or resolve_report(evidence_root)
    except EvidenceError:
        candidate = Path(evidence_root) / 'missing-coordinator-report'
    try:
        approved_report = reference_report or resolve_report(reference_root)
    except EvidenceError:
        approved_report = Path(reference_root) / 'missing-coordinator-report'
    return Analyzer(source_repo, expected_commit, expected_tree, node).run(
        candidate, evidence_root, reference_root, approved_report)


def publish(result, output):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    write_json(output / 'analysis.json', result)
    lines = ['# Deterministic validation evidence', '**' + result['classification'] + '**',
             'Coordinator verdict: ' + str(result['coordinatorVerdict']),
             'Gates: ' + json.dumps(result['gateCounts'], sort_keys=True),
             'Discrepancies: ' + str(len(result['discrepancies'])),
             'This analyzer executed without model calls. Implementation and investigation usage are separate.']
    lines.extend('- ' + i['code'] + ': ' + i['path'] for i in result['discrepancies'])
    (output / 'report.md').write_text('\n\n'.join(lines) + '\n', encoding='utf-8')


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ['evidence-root', 'reference-root', 'source-repo', 'expected-commit', 'expected-tree', 'out']:
        p.add_argument('--' + name, required=True)
    p.add_argument('--report-dir')
    p.add_argument('--reference-report')
    p.add_argument('--node', default='node')
    return p


if __name__ == '__main__':
    options = vars(parser().parse_args())
    output = options.pop('out')
    try:
        result = analyze(**options)
    except (EvidenceError, OSError, ValueError, subprocess.SubprocessError) as error:
        result = dict(schemaVersion=1, classification='evidence_incomplete', authoritative=False,
                      coordinatorVerdict=None, gateCounts={}, discrepancies=[dict(code='invalid_input', path=str(error))])
    publish(result, output)
    print(json.dumps(dict(classification=result['classification'], output=str(output))))
    raise SystemExit(0 if result['authoritative'] else 1)
