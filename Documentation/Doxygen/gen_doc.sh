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
# Remove only this checkout's generated HTML so obsolete pages are not shipped.
doc_dir=$(cd -- "$script_dir/.." && pwd)
html_dir="$doc_dir/html"
[[ "$script_dir" == "$doc_dir/Doxygen" && ! -L "$html_dir" ]] || {
  echo "Unexpected documentation output directory: $html_dir" >&2; exit 1;
}
rm -rf -- "$html_dir"
"${DOXYGEN:-doxygen}" c_mcp.dxy
test -s ../html/index.html
# CMSIS navigation and search assets replace Doxygen's generated defaults.
cp style_template/search.css ../html/search/search.css
cp style_template/navtree.js style_template/resize.js ../html/
sed "s/{projectNumber}/$version/g" style_template/footer.js.in > ../html/footer.js
echo "Documentation generated: $(cd ../html && pwd)/index.html"
