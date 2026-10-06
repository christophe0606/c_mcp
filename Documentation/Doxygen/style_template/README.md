# CMSIS documentation template

These assets come from
[CMSIS-DSP's Doxygen template](https://github.com/ARM-software/CMSIS-DSP/tree/83a2d7bc98c81b4bbe4a6f48b1f2ecf179868a0b/Documentation/Doxygen/style_template).
The stylesheets, logo, navigation, resize, search and theme scripts retain the
CMSIS appearance and their original license notices. CMSIS-DSP is used only
as a read-only reference.

The header fixes the logo image markup and uses the CMSIS-MCP project version.
The footer reports CMSIS-MCP's generated version and license. The existing
`version-loader.js` loads the shared `gh-pages` version selector on the
published site; local HTML uses the version label without requesting it.

`gen_doc.sh` copies the CMSIS navigation, resize and search assets over
Doxygen's generated defaults. Keep these copies and the Doxygen configuration
together when updating the template.
