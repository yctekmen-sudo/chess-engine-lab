#!/usr/bin/env bash
set -euo pipefail
if [[ $# -ne 9 ]]; then echo 'Expected WindowsSource Evidence Kit RunID Depth Reps From To TimeoutSeconds' >&2; exit 2; fi
WINDOWS_SOURCE="$1"; EVIDENCE="$2"; KIT="$3"; RUNID="$4"; DEPTH="$5"; REPS="$6"; FROM="$7"; TO="$8"; TIMEOUT="$9"
export NVM_DIR="${NVM_DIR:-$HOME/.nvm}"
[[ -s "$NVM_DIR/nvm.sh" ]] || { echo 'Missing WSL nvm' >&2; exit 2; }
# shellcheck disable=SC1090
source "$NVM_DIR/nvm.sh"
nvm use --silent 24.10.0 >/dev/null
[[ "$(node --version)" == 'v24.10.0' ]] || { echo 'Need WSL Node 24.10.0' >&2; exit 2; }
REPO="$HOME/work/celab-mates-s0-${RUNID}/linux"
[[ ! -e "$REPO" ]] || { echo 'Refusing to reuse WSL path' >&2; exit 2; }
mkdir -p "$(dirname "$REPO")"
git clone --no-hardlinks --no-checkout "$WINDOWS_SOURCE" "$REPO"
git -C "$REPO" checkout --detach f92a170deee6d9e56b173101ad50aca6c619ab6d
[[ "$(git -C "$REPO" rev-parse HEAD)" == 'f92a170deee6d9e56b173101ad50aca6c619ab6d' ]]
[[ -z "$(git -C "$REPO" status --porcelain --untracked-files=all)" ]]
cmake -S "$REPO" -B "$REPO/build/native-release" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build "$REPO/build/native-release" --parallel 2
npm --prefix "$REPO/controller" ci --ignore-scripts
ctest --test-dir "$REPO/build/native-release" --output-on-failure
node "$KIT/bench-mates.mjs" --repo "$REPO" --engine "$REPO/build/native-release/chess_lab_uci" \
 --corpus "$EVIDENCE/frozen-mates.json" --out "$EVIDENCE/linux.jsonl" --platform linux \
 --depth "$DEPTH" --repetitions "$REPS" --mate-from "$FROM" --mate-to "$TO" --timeout-seconds "$TIMEOUT"
python3 - "$EVIDENCE/linux-identity.json" "$REPO" <<'PY'
import json,pathlib,subprocess,sys
out=pathlib.Path(sys.argv[1]);repo=sys.argv[2]
def g(*args):return subprocess.check_output(['git','-C',repo,*args],text=True).strip()
record={'sha':g('rev-parse','HEAD'),'tree':g('rev-parse','HEAD^{tree}'),
        'trackedChanges':g('diff','HEAD','--name-only'),'untracked':g('ls-files','--others','--exclude-standard')}
out.write_text(json.dumps(record,indent=2)+'\n')
PY
echo 'LINUX_MATES_RUN_COMPLETE'
