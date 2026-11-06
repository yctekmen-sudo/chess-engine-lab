"""Shared A2-compatible evidence operations; Python standard library only."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


class EvidenceError(ValueError):
    pass


def read_json(path):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise EvidenceError('Duplicate JSON key: ' + key)
            result[key] = value
        return result
    try:
        return json.loads(Path(path).read_text(encoding='utf-8-sig'), object_pairs_hook=unique,
                          parse_constant=lambda v: (_ for _ in ()).throw(EvidenceError(v)))
    except (OSError, ValueError) as error:
        raise EvidenceError(f'{path}: {error}') from error


def write_json(path, value):
    path = Path(path)
    path.write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + '\n', encoding='utf-8')


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def contained(root, relative):
    root = Path(root).resolve()
    path = (root / relative).resolve()
    if not path.is_relative_to(root):
        raise EvidenceError(f'Evidence path escapes root: {relative}')
    return path


def command(args, **kwargs):
    """Argument arrays, no shell; callers supply bounded timeouts for probes."""
    return subprocess.run([str(x) for x in args], check=True, capture_output=True,
                          text=True, encoding='utf-8', env={**os.environ, 'GIT_OPTIONAL_LOCKS': '0'},
                          **kwargs).stdout.strip()


def git(repo, *args):
    return command(['git', '-C', repo, *args], timeout=30)


def bridge(repo, action, node='node', **data):
    return json.loads(command([node, Path(__file__).with_name('ci-contract.mjs')],
        input=json.dumps(dict(repo=str(repo), action=action, **data)), timeout=60))


def stable_records(path):
    """Same printed-record extraction/seconds-only normalization used by A2."""
    records = []
    for line in Path(path).read_text(encoding='utf-8', errors='strict').splitlines():
        if re.match(r'^(initial|kiwipete|rook-pawn EP|promotions/castling|promotion/check|middlegame) depth \d+: \d+$', line) or line.startswith((
                'M7.2 end ', 'compiled_ordering_policy=', 'P1 passed searches=')):
            records.append(re.sub(r'\bseconds=[0-9.eE+-]+', 'seconds=<excluded>', line))
    return records


def records_hash(records):
    return hashlib.sha256(('\n'.join(records) + '\n').encode()).hexdigest()


def runtime_tools(tools):
    """Paths, probe durations and banners' line endings are runtime variables."""
    return {key: ({'version': value.get('version', '').replace('\r\n', '\n'),
                   'exitCode': value.get('exitCode')} if isinstance(value, dict) else value)
            for key, value in tools.items()}


def manifest(root, paths=None):
    root = Path(root).resolve()
    files = paths if paths is not None else [p for p in root.rglob('*') if p.is_file()]
    entries = []
    for path in sorted(set(Path(p).resolve() for p in files)):
        if path.name == 'evidence-manifest.json':
            continue
        relative = path.relative_to(root).as_posix()
        entries.append(dict(path=relative, bytes=path.stat().st_size, sha256=digest(path)))
    return dict(files=entries, rawSessionFilesIncluded=False)
