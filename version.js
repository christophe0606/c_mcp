// SPDX-License-Identifier: Apache-2.0
// Adapted from ARM-software/CMSIS-DSP's gh-pages version selector.
// https://github.com/ARM-software/CMSIS-DSP/tree/gh-pages
//--- list of versions ---
const versions = {
    "main": "0.1.0"
}
//--- list of versions ---

;(function () {
  const script = document.currentScript;
  const target = document.getElementById("projectnumber");
  if (!script || !target) return;
  const base = new URL("./", script.src);
  const current = new URL(document.URL);
  if (!current.pathname.startsWith(base.pathname)) return;
  const parts = current.pathname.slice(base.pathname.length).split("/");
  parts.shift();
  const dropdown = document.createElement("div");
  dropdown.className = "dropdown-content";
  dropdown.id = "version-dropdown";
  const button = document.createElement("button");
  button.type = "button";
  button.className = "dropbtn";
  button.textContent = target.textContent.trim();
  button.setAttribute("aria-expanded", "false");
  button.setAttribute("aria-controls", dropdown.id);
  for (const [version, value] of Object.entries(versions)) {
    const link = document.createElement("a");
    const url = new URL(encodeURIComponent(version) + "/" + parts.join("/"), base);
    url.search = current.search;
    url.hash = current.hash;
    link.href = url.href;
    link.textContent = "Version " + value + (value === version ? "" : " (" + version + ")");
    dropdown.appendChild(link);
  }
  target.textContent = "";
  target.classList.add("dropdown");
  target.appendChild(button);
  target.appendChild(dropdown);
  button.addEventListener("click", function () {
    const open = dropdown.classList.toggle("show");
    button.setAttribute("aria-expanded", String(open));
  });
  document.addEventListener("click", function (event) {
    if (!target.contains(event.target)) {
      dropdown.classList.remove("show");
      button.setAttribute("aria-expanded", "false");
    }
  });
})();
