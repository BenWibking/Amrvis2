#!/usr/bin/env bash
# PROTOTYPE: one-command build and real localhost request/response demo.
set -euo pipefail

prototype_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source_dir="$(cd "${prototype_dir}/../.." && pwd)"
build_dir="${source_dir}/build-wire-prototype"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/amrvis-wire-prototype.XXXXXX")"
server_pid=""
cd "${source_dir}"

cleanup() {
    if [[ -n "${server_pid}" ]] && kill -0 "${server_pid}" 2>/dev/null; then
        kill "${server_pid}" 2>/dev/null || true
        wait "${server_pid}" 2>/dev/null || true
    fi
    rm -rf "${work_dir}"
}
trap cleanup EXIT

cmake --preset wire-prototype -S "${source_dir}"
cmake --build --preset wire-prototype \
    --target fixture_materializer amrvis_wire_server amrvis_wire_client

fixture="${work_dir}/plotfile_2d"
"${build_dir}/tests/fixture_materializer" \
    "${source_dir}/tests/data/plotfile_2d" "${fixture}"

server_log="${work_dir}/server.log"
"${build_dir}/prototypes/flatbuffers_wire/amrvis_wire_server" \
    --port 0 >"${server_log}" 2>&1 &
server_pid="$!"

for _ in {1..100}; do
    if grep -q '^LISTENING ' "${server_log}"; then
        break
    fi
    if ! kill -0 "${server_pid}" 2>/dev/null; then
        wait "${server_pid}" || true
        sed -n '1,160p' "${server_log}"
        exit 1
    fi
    sleep 0.05
done

port="$(awk '/^LISTENING / { print $3; exit }' "${server_log}")"
if [[ -z "${port}" ]]; then
    echo "server did not report a listening port" >&2
    exit 1
fi

"${build_dir}/prototypes/flatbuffers_wire/amrvis_wire_client" \
    127.0.0.1 "${port}" "${fixture}"
wait "${server_pid}"
server_pid=""

echo
echo "SERVER"
sed -n '1,160p' "${server_log}"
