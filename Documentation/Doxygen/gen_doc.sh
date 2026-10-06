#!/usr/bin/env bash
# Generate c_mcp's Doxygen HTML, from any working directory.
set -euo pipefail

case "${1:-}" in
  -h|--help)
    echo "Usage: bash Documentation/Doxygen/gen_doc.sh"
    exit 0 ;;
  "") ;;
  *) echo "Unknown argument: $1" >&2; exit 2 ;;
esac

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cd "$script_dir"
version=$(sed -n 's/.*<release version="\([^"]*\)".*/\1/p' ../../ARM.CMSIS-MCP.pdsc | head -n 1)
[[ -n "$version" ]] || { echo "No release version in PDSC" >&2; exit 1; }
sed "s/{projectNumber}/$version/g" c_mcp.dxy.in > c_mcp.dxy
"${DOXYGEN:-doxygen}" c_mcp.dxy
test -s ../html/index.html
echo "Documentation generated: $(cd ../html && pwd)/index.html"
