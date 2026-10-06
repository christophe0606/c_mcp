// SPDX-License-Identifier: Apache-2.0
// Load the shared version selector on the published documentation site.
(function () {
  const script = document.currentScript;
  if (!script || !/^https?:$/.test(location.protocol)) return;
  if (!new URL(script.src).pathname.startsWith("/c_mcp/")) return;
  const root = new URL("../", script.src);
  const stylesheet = document.createElement("link");
  stylesheet.rel = "stylesheet";
  stylesheet.href = new URL("version.css", root).href;
  document.head.appendChild(stylesheet);
  const versions = document.createElement("script");
  versions.src = new URL("version.js", root).href;
  document.head.appendChild(versions);
})();
