// SPDX-License-Identifier: Apache-2.0
// Load the shared version selector on the published documentation site.
(function () {
  const script = document.currentScript;
  if (!script || location.hostname !== "christophe0606.github.io") return;
  const root = new URL("../", script.src);
  const stylesheet = document.createElement("link");
  stylesheet.rel = "stylesheet";
  stylesheet.href = new URL("version.css", root).href;
  document.head.appendChild(stylesheet);
  const versions = document.createElement("script");
  versions.src = new URL("version.js", root).href;
  document.head.appendChild(versions);
})();
