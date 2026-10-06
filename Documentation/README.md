# Documentation

From the repository root, run:

```sh
bash Documentation/Doxygen/gen_doc.sh
```

Requires Bash and Doxygen (1.13.2 is used in CI). The script uses
`Doxygen/c_mcp.dxy.in`, the public headers, `LICENSE.md`, and the overview and
integration guide in `Doxygen/src/`. HTML is written to `Documentation/html/`.
Set `DOXYGEN` to an executable path if Doxygen is not on PATH.

The HTML uses the CMSIS template from CMSIS-DSP, including the CMSIS logo,
header, colour themes, navigation and search styling. See
`Doxygen/style_template/README.md` for the source revision and adaptations.

The pack workflow generates this HTML and publishes it under `main/` on the
`gh-pages` branch. Pull requests receive a documentation artifact instead.
The Pages workflow deploys it at
https://christophe0606.github.io/c_mcp/main/.

The `gh-pages` branch also contains the shared `version.js`, `version.css`
and `dropdown.png` assets, an `update_versions.sh` script, and the root
redirect to `main/`. Generated `footer.js` metadata supplies the version
label. The shared selector is loaded on the published site; local HTML keeps
the normal Doxygen version label.
