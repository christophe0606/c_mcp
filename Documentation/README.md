# Documentation

From the repository root, run:

```sh
bash Documentation/Doxygen/gen_doc.sh
```

Requires Bash and Doxygen (1.13.2 is used in CI). The script uses
`Doxygen/c_mcp.dxy.in`, the public headers, `LICENSE.md`, and the overview and
integration guide in `Doxygen/src/`. HTML is written to `Documentation/html/`.
Set `DOXYGEN` to an executable path if Doxygen is not on PATH.

The pack workflow generates and uploads this HTML. Documentation publishing
and the gh-pages branch will be configured separately.
