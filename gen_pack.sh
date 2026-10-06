#!/usr/bin/env bash
# Generate a source CMSIS-Pack with Doxygen HTML and the host serial bridge.
# Requires Bash, Doxygen, PackChk, zip, and the ARM.CMSIS dependency PDSC.
set -euo pipefail

preprocess=1
while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-preprocess) preprocess=0 ;;
    -h|--help)
      echo "Usage: bash gen_pack.sh [--no-preprocess]"
      echo "Environment: PACK_OUTPUT, PACKCHK, CMSIS_PDSC, CMSIS_PACK_ROOT, DOXYGEN"
      exit 0 ;;
    *) echo "Unknown argument: $1" >&2; exit 2 ;;
  esac
  shift
done

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cd "$root"
if [[ $preprocess == 1 ]]; then
  bash Documentation/Doxygen/gen_doc.sh
fi
test -s Documentation/html/index.html

packchk=${PACKCHK:-packchk}
command -v "$packchk" >/dev/null || { echo "PackChk is required (set PACKCHK or PATH)." >&2; exit 1; }
command -v zip >/dev/null || { echo "zip is required." >&2; exit 1; }

cmsis_pdsc=${CMSIS_PDSC:-}
if [[ -z "$cmsis_pdsc" && -n "${CMSIS_PACK_ROOT:-}" ]]; then
  if [[ -f "$CMSIS_PACK_ROOT/.Web/ARM.CMSIS.pdsc" ]]; then
    cmsis_pdsc="$CMSIS_PACK_ROOT/.Web/ARM.CMSIS.pdsc"
  else
    cmsis_pdsc=$(find "$CMSIS_PACK_ROOT/ARM/CMSIS" -name ARM.CMSIS.pdsc | sort -V | tail -n 1)
  fi
fi
[[ -f "$cmsis_pdsc" ]] || { echo "Set CMSIS_PDSC to ARM.CMSIS.pdsc, or CMSIS_PACK_ROOT to your pack cache." >&2; exit 1; }
cmsis_pdsc=$(cd -- "$(dirname -- "$cmsis_pdsc")" && pwd)/$(basename -- "$cmsis_pdsc")

mkdir -p build "${PACK_OUTPUT:-output}"
output=$(cd -- "${PACK_OUTPUT:-output}" && pwd)
# Use a fresh directory and archive so removed files cannot survive a rebuild.
stage=$(mktemp -d "$root/build/pack.XXXXXXXX")
archive=""
cleanup() {
  # mktemp created stage under this repository's build directory.
  [[ "$stage" == "$root/build/pack."* ]] && rm -rf -- "$stage"
  [[ -z "$archive" ]] || rm -f -- "$archive"
}
trap cleanup EXIT

cp ARM.CMSIS-MCP.pdsc README.md LICENSE.md \
   mcp.c mcp.h mcp_index.h cJSON.c cJSON.h serial_transport.c serial_transport.h "$stage/"
cp -R Config overview "$stage/"
mkdir -p "$stage/tools"
cp tools/SerialBridge.clayer.yml tools/README.md \
   tools/mcp_serial_bridge.py tools/requirements-mcp-serial-bridge.txt "$stage/tools/"
mkdir -p "$stage/Documentation"
cp -R Documentation/html "$stage/Documentation/"
# Only the runtime bridge is shipped, not Python caches or test fixtures.
"$packchk" "$stage/ARM.CMSIS-MCP.pdsc" -i "$cmsis_pdsc" -n "$stage/PackName.txt"
pack_name=$(tr -d '\r\n' < "$stage/PackName.txt")
[[ "$pack_name" == ARM.CMSIS-MCP.*.pack && "$pack_name" != */* ]] || { echo "Invalid pack name: $pack_name" >&2; exit 1; }
rm -- "$stage/PackName.txt"
archive="$output/.$pack_name.$$.pack"
(cd "$stage" && zip -q -r "$archive" .)
mv -f -- "$archive" "$output/$pack_name"
archive=""
echo "Pack generated: $output/$pack_name"
