#!/usr/bin/env bash
# Noninteractive bootstrap; this file may live in the Windows checkout while
# commands execute against a separately maintained native Linux checkout.
set -euo pipefail
ci_worker_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ci_repo='' ci_report='' ci_action=run ci_mode=full ci_areas=infrastructure
ci_sdk=${CHESS_LAB_CI_EMSDK:-"$HOME/toolchains/emsdk"}
ci_node_version=${CHESS_LAB_CI_NODE_VERSION:-24.10.0}
while (($#)); do
  case "$1" in
    --repo) ci_repo=$2; shift 2;;
    --report) ci_report=$2; shift 2;;
    --action) ci_action=$2; shift 2;;
    --mode) ci_mode=$2; shift 2;;
    --areas) ci_areas=$2; shift 2;;
    --emsdk) ci_sdk=$2; shift 2;;
    --node-version) ci_node_version=$2; shift 2;;
    --sdk-exec) ci_action=sdk-exec; shift; break;;
    *) echo "Unknown Linux worker argument: $1" >&2; exit 2;;
  esac
done
if [[ $ci_action == sdk-exec ]]; then
  [[ -s "$ci_sdk/emsdk_env.sh" ]] || { echo "Missing emsdk_env.sh: $ci_sdk" >&2; exit 2; }
  export EMSDK_QUIET=1
  set +u
  source "$ci_sdk/emsdk_env.sh" >/dev/null
  set -u
  [[ $(git -C "$ci_sdk" rev-parse HEAD) == 62a853cd3b3134398ce85cde8bb5cbb2ef0194cb ]] || { echo 'SDK checkout does not match the CI pin' >&2; exit 2; }
  ci_emcc_version=$(emcc --version)
  [[ $ci_emcc_version == *'4.0.10'* ]] || { echo 'Emscripten 4.0.10 required' >&2; exit 2; }
  [[ -x ${EMSDK_NODE:-} ]] && [[ $("$EMSDK_NODE" --version) == v22.16.0 ]] || { echo 'SDK Node 22.16.0 required' >&2; exit 2; }
  export CHESS_LAB_CI_SDK_NODE="$EMSDK_NODE"
  export CHESS_LAB_CI_SDK_ROOT="$ci_sdk"
  # An explicit configure argument avoids a stale/system Node in CMake's cache.
  if [[ ${1:-} == wasm-configure ]]; then
    shift
    exec cmake --preset wasm-debug "-DCHESS_LAB_NODE_EXECUTABLE=$EMSDK_NODE" "$@"
  fi
  exec "$@"
fi
[[ -n $ci_repo && -n $ci_report ]] || { echo '--repo and --report are required' >&2; exit 2; }
# Never pull, reset, copy or checkout source here.
cd -- "$ci_repo"
source /etc/os-release
[[ $ID == ubuntu && $VERSION_ID == 24.04 ]] || { echo 'Ubuntu 24.04 required' >&2; exit 2; }
export NVM_DIR=${NVM_DIR:-"$HOME/.nvm"}
[[ -s "$NVM_DIR/nvm.sh" ]] || { echo "Missing nvm: $NVM_DIR/nvm.sh" >&2; exit 2; }
set +u
source "$NVM_DIR/nvm.sh"
if ! nvm use --silent "$ci_node_version"; then
  echo "Node $ci_node_version is unavailable in nvm; install it explicitly before running local CI." >&2
  exit 2
fi
set -u
[[ $(node --version) == "v$ci_node_version" ]] || { echo "Node $ci_node_version required" >&2; exit 2; }
export CHESS_LAB_CI_EMSDK="$ci_sdk"
exec node "$ci_worker_dir/suite.mjs" "$ci_action" --platform linux --repo "$ci_repo" --report "$ci_report" --mode "$ci_mode" --areas "$ci_areas"
