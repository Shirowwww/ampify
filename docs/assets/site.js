// The site's few behaviours: copy buttons, the theme carousel of the home page, smooth scrolling, and the galleries,
// which read the public catalog. Everything from the catalog is put in the page as text, never as HTML.
(() => {
  "use strict";

  const reducedMotion = window.matchMedia("(prefers-reduced-motion: reduce)");
  const CATALOG = "https://raw.githubusercontent.com/Shirowwww/ampify-catalog/main/catalog.json";
  const ROTATE_MS = 5000;
  const COPIED_MS = 1600;

  function el(tag, props = {}, children = []) {
    const node = document.createElement(tag);
    for (const [key, value] of Object.entries(props)) {
      if (key === "text") node.textContent = value;
      else if (key === "class") node.className = value;
      else node.setAttribute(key, value);
    }
    for (const child of children) if (child) node.append(child);
    return node;
  }

  async function copyText(text) {
    if (navigator.clipboard && window.isSecureContext) {
      await navigator.clipboard.writeText(text);
      return;
    }
    const area = el("textarea", { readonly: "", style: "position:fixed;opacity:0" });
    area.value = text;
    document.body.append(area);
    area.select();
    document.execCommand("copy");
    area.remove();
  }

  function wireCopy(button, getText) {
    const label = button.textContent;
    button.addEventListener("click", async () => {
      try {
        await copyText(getText());
        button.textContent = button.dataset.done || label;
        button.classList.add("done");
        setTimeout(() => {
          button.textContent = label;
          button.classList.remove("done");
        }, COPIED_MS);
      } catch {
        button.textContent = label;
      }
    });
  }

  function initCopyButtons() {
    for (const button of document.querySelectorAll("button[data-copy]")) {
      const target = document.getElementById(button.dataset.copy);
      if (target) wireCopy(button, () => target.textContent.trim());
    }
  }

  // The home page's window shows one theme at a time; the chips pick one, and it moves on alone unless the visitor
  // points at it, focuses it, has asked for less motion, or the tab is hidden.
  function initRotator(stage) {
    const images = [...stage.querySelectorAll("img[data-theme-id]")];
    const chips = [...stage.querySelectorAll("button[data-show]")];
    if (images.length < 2) return;
    let current = 0;
    let timer = 0;
    let held = false;

    const show = (index) => {
      current = (index + images.length) % images.length;
      images.forEach((img, i) => {
        img.classList.toggle("on", i === current);
        if (i === current) img.loading = "eager";
      });
      chips.forEach((chip) => chip.setAttribute("aria-pressed", String(chip.dataset.show === images[current].dataset.themeId)));
    };
    const stop = () => { clearInterval(timer); timer = 0; };
    const start = () => {
      stop();
      if (!held && !reducedMotion.matches && !document.hidden) timer = setInterval(() => show(current + 1), ROTATE_MS);
    };
    const hold = (value) => { held = value; start(); };

    chips.forEach((chip) => chip.addEventListener("click", () => {
      show(images.findIndex((img) => img.dataset.themeId === chip.dataset.show));
      hold(true);
    }));
    stage.addEventListener("pointerenter", () => hold(true));
    stage.addEventListener("pointerleave", () => hold(false));
    stage.addEventListener("focusin", () => hold(true));
    stage.addEventListener("focusout", () => hold(false));
    document.addEventListener("visibilitychange", start);
    reducedMotion.addEventListener("change", start);
    start();
  }

  function initSmoothScroll() {
    if (reducedMotion.matches || typeof window.Lenis !== "function") return;
    const lenis = new window.Lenis({ lerp: 0.12, smoothWheel: true });
    const frame = (time) => {
      lenis.raf(time);
      requestAnimationFrame(frame);
    };
    requestAnimationFrame(frame);
  }

  // The docs are written for GitHub, where they link to files outside docs/ (examples, knowledge notes). On the site
  // those links leave it: they go to the file on GitHub instead.
  function initRepoLinks() {
    const { base, repo } = document.body.dataset;
    if (!base || !repo) return;
    for (const link of document.querySelectorAll(".prose a[href]")) {
      const url = new URL(link.href, location.href);
      if (url.origin !== location.origin || url.pathname.startsWith(base)) continue;
      // The documentation index turns README.md's .md links into .html ones; outside docs/ only the .md exists.
      const path = url.pathname.replace(/^\/+/, "").replace(/\.html$/, ".md");
      const isFile = /\.[a-z0-9]+$/i.test(path);
      link.href = `${repo}/${isFile ? "blob" : "tree"}/main/${path.replace(/\/$/, "")}${url.hash}`;
    }
  }

  // ---- Galleries ------------------------------------------------------------------------------------------------

  function card(item, texts) {
    const command = `ampify install ${item.id}`;
    const code = el("code", { text: command });
    const copy = el("button", { type: "button", class: "copy", text: texts.copy, "data-done": texts.copied });
    wireCopy(copy, () => command);

    const shot = typeof item.preview === "string" && item.preview.startsWith("https://")
      ? el("div", { class: "shot" }, [el("img", { src: item.preview, alt: "", loading: "lazy", width: "1280", height: "720" })])
      : el("div", { class: "shot glyph", "aria-hidden": "true", text: item.name.slice(0, 1) });
    const permissions = item.permissions && item.permissions.length
      ? `${texts.permissions}: ${item.permissions.join(", ")}`
      : texts.noPermission;

    return el("article", { class: "item" }, [
      shot,
      el("h3", {}, [document.createTextNode(item.name), el("small", { text: `v${item.version}` })]),
      el("p", { text: item.description }),
      el("div", { class: "tags" }, (item.tags || []).map((tag) => el("span", { text: tag }))),
      item.kind === "theme" ? null : el("p", { class: "meta", text: permissions }),
      el("p", { class: "meta", text: `${texts.by} ${item.author}` }),
      el("div", { class: "cmd" }, [code, copy]),
    ]);
  }

  function renderFilters(gallery, items, onChange) {
    const tags = [...new Set(items.flatMap((item) => item.tags || []))].sort();
    const search = el("input", { type: "search", placeholder: gallery.dataset.search, "aria-label": gallery.dataset.search });
    const bar = el("div", { class: "filters" }, [search]);
    const state = { query: "", tag: "" };
    const buttons = tags.map((tag) => {
      const button = el("button", { type: "button", "aria-pressed": "false", text: tag });
      button.addEventListener("click", () => {
        state.tag = state.tag === tag ? "" : tag;
        buttons.forEach((b) => b.setAttribute("aria-pressed", String(b.textContent === state.tag)));
        onChange(state);
      });
      return button;
    });
    bar.append(...buttons);
    search.addEventListener("input", () => { state.query = search.value.trim().toLowerCase(); onChange(state); });
    return bar;
  }

  function matches(item, state) {
    if (state.tag && !(item.tags || []).includes(state.tag)) return false;
    if (!state.query) return true;
    const haystack = [item.name, item.id, item.description, ...(item.tags || [])].join(" ").toLowerCase();
    return haystack.includes(state.query);
  }

  async function initGallery(gallery) {
    const status = gallery.querySelector(".status");
    const texts = {
      copy: gallery.dataset.copy, copied: gallery.dataset.copied, by: gallery.dataset.by,
      permissions: gallery.dataset.permissions, noPermission: gallery.dataset.noPermission,
    };
    try {
      const response = await fetch(CATALOG, { cache: "no-cache" });
      if (!response.ok) throw new Error(String(response.status));
      const catalog = await response.json();
      const kinds = gallery.dataset.kinds.split(",");
      const items = (catalog.extensions || [])
        .filter((item) => kinds.includes(item.kind) && typeof item.id === "string" && typeof item.name === "string")
        .sort((a, b) => a.name.localeCompare(b.name));
      const grid = el("div", { class: "grid" });
      const draw = (state) => {
        const shown = items.filter((item) => matches(item, state));
        grid.replaceChildren(...shown.map((item) => card(item, texts)));
        status.textContent = shown.length ? "" : gallery.dataset.empty;
        status.hidden = shown.length > 0;
      };
      gallery.append(renderFilters(gallery, items, draw), grid);
      draw({ query: "", tag: "" });
    } catch {
      status.textContent = gallery.dataset.error;
    }
  }

  document.addEventListener("DOMContentLoaded", () => {
    initCopyButtons();
    initRepoLinks();
    document.querySelectorAll("[data-rotate]").forEach(initRotator);
    document.querySelectorAll("[data-gallery]").forEach(initGallery);
    initSmoothScroll();
  });
})();
