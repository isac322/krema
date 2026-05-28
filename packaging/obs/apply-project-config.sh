#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project="${OBS_PROJECT:-home:isac322}"
osc_bin="${OSC:-osc}"

"${osc_bin}" api -X PUT -T "${script_dir}/project.meta.xml" "/source/${project}/_meta"
"${osc_bin}" api -X PUT -T "${script_dir}/project.config" "/source/${project}/_config"
"${osc_bin}" results "${project}" krema
