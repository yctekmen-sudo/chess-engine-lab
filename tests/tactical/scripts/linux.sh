#!/usr/bin/env bash
set -euo pipefail
if [[ $# -ne 7 ]]; then echo 'linux.sh expects: WINDOWS_SOURCE EVIDENCE KIT RUNID DEPTH REPS LIMIT' >&2; exit 2; fi
WINDOWS_SOURCE="$1"; EVIDENCE="$2"; KIT="$3"; RUNID="$4"; DEPTH="$5"; REPS="$6"; LIMIT="$7"
if [[ ! -d "$WINDOWS_SOURCE/.git" ]]; then echo "Not an ordinary source clone: $WINDOWS_SOURCE" >&2; exit 2; fi
export NVM_DIR="${NVM_DIR:-$HOME/.nvm}"
if [[ ! -s "$NVM_DIR/nvm.sh" ]]; then echo "Node nvm not found: $NVM_DIR" >&2; exit 2; fi
# shellcheck disable=SC1090
source "$NVM_DIR/nvm.sh"
nvm use --silent 24.10.0 >/dev/null
if [[ "$(node --version)" != 'v24.10.0' ]]; then echo 'WSL Node 24.10.0 required' >&2; exit 2; fi
REPO="$HOME/work/celab-tactics-s0-${RUNID}/linux"
if [[ -e "$REPO" ]]; then echo "Refusing to reuse prior WSL source: $REPO" >&2; exit 2; fi
mkdir -p "$(dirname "$REPO")"
git clone --no-hardlinks --no-checkout "$WINDOWS_SOURCE" "$REPO"
git -C "$REPO" checkout --detach f92a170deee6d9e56b173101ad50aca6c619ab6d
[[ "$(git -C "$REPO" rev-parse HEAD)" == 'f92a170deee6d9e56b173101ad50aca6c619ab6d' ]]
[[ -z "$(git -C "$REPO" status --porcelain --untracked-files=all)" ]]
cmake -S "$REPO" -B "$REPO/build/native-release" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build "$REPO/build/native-release" --parallel 2
npm --prefix "$REPO/controller" ci --ignore-scripts
ctest --test-dir "$REPO/build/native-release" --output-on-failure
ENGINE="$REPO/build/native-release/chess_lab_uci"
[[ -x "$ENGINE" ]] || { echo 'Linux UCI executable missing' >&2; exit 2; }
CMD=(node "$KIT/bench.mjs" --engine "$ENGINE" --corpus "$EVIDENCE/frozen-corpus.json" --out "$EVIDENCE/linux.jsonl" --platform linux --depth "$DEPTH" --repetitions "$REPS")
if [[ "$LIMIT" != '0' ]]; then CMD+=(--limit "$LIMIT"); fi
"${CMD[@]}"
python3 - "$EVIDENCE/linux-identity.json" "$REPO" <<'PY'
import json,pathlib,subprocess,sys
out=pathlib.Path(sys.argv[1]);repo=sys.argv[2]
def g(*args):return subprocess.check_output(['git','-C',repo,*args],text=True).strip()
record={'sha':g('rev-parse','HEAD'),'tree':g('rev-parse','HEAD^{tree}'),'trackedChanges':g('diff','HEAD','--name-only'),
        'untracked':g('ls-files','--others','--exclude-standard')}
out.write_text(json.dumps(record,indent=2)+'\n')
PY
echo 'LINUX_BASELINE_COMPLETE'
