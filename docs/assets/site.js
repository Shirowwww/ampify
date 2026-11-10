// The site's behaviours: copy buttons, the theme carousel of the home page, smooth scrolling, reveal on scroll, and
// the Marketplace, which reads the public catalog and reads it again while the page stays open. Everything from the
// catalog is put in the page as text, never as HTML.
(() => {
  "use strict";

  const reducedMotion = window.matchMedia("(prefers-reduced-motion: reduce)");
  // The 1.3 catalog first; catalog.json stays frozen for Ampify 1.2.1 and is the fallback while the other is missing.
  const CATALOGS = [
    "https://raw.githubusercontent.com/Shirowwww/ampify-catalog/main/catalog-1.3.json",
    "https://raw.githubusercontent.com/Shirowwww/ampify-catalog/main/catalog.json",
  ];
  const KINDS = ["theme", "snippet", "extension", "app"];
  // Retired in 1.3 (src/core/theme/retired.cpp): the catalog still lists them for Ampify 1.2.1, Ampify hides them.
  const RETIRED = new Set([
    "animated-artwork", "bottom-playbar", "centred-lyrics", "compact-sidebar", "cover-ambience", "dark-lyrics",
    "ember", "focused-lyrics", "hide-buttons", "hide-explicit", "high-contrast", "hover-row-actions", "hover-search",
    "lyrics-spotlight", "playbar-times", "progress-mascot", "progress-thickness", "sidebar-icons", "square-corners",
  ]);
  // An entry is its first version; later ones are in `newer`, oldest first. The page shows the latest.
  const newestVersion = (item) => {
    const newer = Array.isArray(item.newer) ? item.newer.filter((v) => v && typeof v === "object") : [];
    return newer.length ? { ...item, ...newer[newer.length - 1], kind: item.kind, id: item.id } : item;
  };
  const REFRESH_MS = 5 * 60 * 1000;
  const ROTATE_MS = 5000;
  const COPIED_MS = 1600;
  const STAGGER_MS = 40;
  const STAGGER_MAX = 12;

  function el(tag, props = {}, children = []) {
    const node = document.createElement(tag);
    for (const [key, value] of Object.entries(props)) {
      if (value === undefined || value === null) continue;
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
    button.addEventListener("click", async (event) => {
      event.stopPropagation();
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

  // ---- Motion -----------------------------------------------------------------------------------------------------

  // Sections and cards rise into place once, the first time they come into view.
  const revealer = "IntersectionObserver" in window && !reducedMotion.matches
    ? new IntersectionObserver((entries) => {
      for (const entry of entries) {
        if (!entry.isIntersecting) continue;
        entry.target.classList.add("in");
        revealer.unobserve(entry.target);
      }
    }, { rootMargin: "0px 0px -8% 0px" })
    : null;

  function reveal(node, index = 0) {
    if (!revealer) return;
    node.classList.add("reveal");
    node.style.setProperty("--delay", `${Math.min(index, STAGGER_MAX) * STAGGER_MS}ms`);
    revealer.observe(node);
  }

  function initReveal() {
    document.querySelectorAll("[data-reveal]").forEach((node) => reveal(node));
    document.querySelectorAll("[data-reveal-children]").forEach((parent) => [...parent.children].forEach(reveal));
  }

  // The home page's window shows one theme at a time; the chips pick one, and it moves on alone unless the visitor
  // points at it, focuses it, has asked for less motion, or the tab is hidden. The glow behind follows the theme.
  function initRotator(stage) {
    const images = [...stage.querySelectorAll(".window img[data-theme-id]")];
    const glows = [...stage.querySelectorAll(".glow img")];
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
      glows.forEach((img, i) => img.classList.toggle("on", i === current));
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
    if (reducedMotion.matches || typeof window.Lenis !== "function") return null;
    const lenis = new window.Lenis({ lerp: 0.12, smoothWheel: true });
    const frame = (time) => {
      lenis.raf(time);
      requestAnimationFrame(frame);
    };
    requestAnimationFrame(frame);
    return lenis;
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

  // ---- Catalog ----------------------------------------------------------------------------------------------------

  /** @returns {Promise<{text: string, items: object[]}>} the first catalog that answers, its items checked */
  async function readCatalog() {
    let lastError = null;
    for (const url of CATALOGS) {
      try {
        const response = await fetch(url, { cache: "no-cache" });
        if (!response.ok) throw new Error(`${url}: ${response.status}`);
        const text = await response.text();
        const catalog = JSON.parse(text);
        const items = (Array.isArray(catalog.extensions) ? catalog.extensions : [])
          .filter((item) => item && KINDS.includes(item.kind) && typeof item.id === "string" && !RETIRED.has(item.id))
          .map(newestVersion)
          .filter((item) => typeof item.name === "string")
          .sort((a, b) => a.name.localeCompare(b.name));
        return { text, items };
      } catch (error) {
        lastError = error;
      }
    }
    throw lastError || new Error("no catalog");
  }

  const isImage = (url) => typeof url === "string" && url.startsWith("https://");
  const asText = (value) => (typeof value === "string" ? value : "");
  const tagsOf = (item) => (Array.isArray(item.tags) ? item.tags.filter((t) => typeof t === "string") : []);
  const permissionsOf = (item) => (Array.isArray(item.permissions) ? item.permissions.filter((p) => typeof p === "string") : []);

  function shot(item, eager) {
    if (!isImage(item.preview)) {
      return el("div", { class: "shot glyph", "aria-hidden": "true" }, [el("span", { text: item.name.slice(0, 1) })]);
    }
    const img = el("img", { src: item.preview, alt: "", loading: eager ? "eager" : "lazy", decoding: "async", width: "1280", height: "720" });
    const frame = el("div", { class: "shot" }, [img]);
    img.addEventListener("load", () => frame.classList.add("loaded"), { once: true });
    img.addEventListener("error", () => frame.replaceWith(shot({ ...item, preview: "" }, false)), { once: true });
    return frame;
  }

  function installRow(item, texts) {
    const command = `ampify install ${item.id}`;
    const copy = el("button", { type: "button", class: "copy", text: texts.copy, "data-done": texts.copied });
    wireCopy(copy, () => command);
    return el("div", { class: "cmd" }, [el("code", { text: command }), copy]);
  }

  function card(item, texts, index, onOpen) {
    const open = el("button", { type: "button", class: "item-open", "aria-haspopup": "dialog" }, [
      el("span", { class: "sr", text: `${texts.details}: ${item.name}` }),
    ]);
    open.addEventListener("click", () => onOpen(item, open));
    const node = el("article", { class: "item", "data-kind": item.kind }, [
      shot(item, index < 3),
      el("div", { class: "item-body" }, [
        el("p", { class: "kind", text: texts.kinds[item.kind] }),
        el("h3", {}, [document.createTextNode(item.name), el("small", { text: `v${asText(item.version)}` })]),
        el("p", { class: "desc", text: asText(item.description) }),
        installRow(item, texts),
      ]),
      open,
    ]);
    return node;
  }

  // ---- Detail dialog ----------------------------------------------------------------------------------------------

  function makeDialog(texts) {
    const dialog = el("dialog", { class: "detail", "aria-labelledby": "detail-title", "data-lenis-prevent": "" });
    document.body.append(dialog);
    let opener = null;

    const close = () => {
      if (!dialog.open || dialog.classList.contains("closing")) return;
      if (reducedMotion.matches) { dialog.close(); return; }
      dialog.classList.add("closing");
      dialog.addEventListener("animationend", () => { dialog.classList.remove("closing"); dialog.close(); }, { once: true });
    };
    dialog.addEventListener("cancel", (event) => { event.preventDefault(); close(); });
    dialog.addEventListener("click", (event) => { if (event.target === dialog) close(); });
    dialog.addEventListener("close", () => {
      if (location.hash) history.replaceState(null, "", location.pathname + location.search);
      if (opener) opener.focus({ preventScroll: true });
    });

    const show = (item, from) => {
      opener = from || null;
      const permissions = permissionsOf(item);
      const facts = el("dl", { class: "facts" }, [
        el("dt", { text: texts.version }), el("dd", { text: asText(item.version) }),
        el("dt", { text: texts.by }), el("dd", { text: asText(item.author) }),
        item.kind === "theme" || item.kind === "snippet" ? null : el("dt", { text: texts.permissions }),
        item.kind === "theme" || item.kind === "snippet" ? null
          : el("dd", { text: permissions.length ? permissions.join(", ") : texts.noPermission }),
      ]);
      const closeButton = el("button", { type: "button", class: "close", "aria-label": texts.close, text: "×" });
      closeButton.addEventListener("click", close);
      dialog.replaceChildren(el("div", { class: "detail-in" }, [
        closeButton,
        shot(item, true),
        el("div", { class: "detail-text" }, [
          el("p", { class: "kind", text: texts.kinds[item.kind] }),
          el("h2", { id: "detail-title", text: item.name }),
          el("p", { class: "desc", text: asText(item.description) }),
          el("div", { class: "tags" }, tagsOf(item).map((tag) => el("span", { text: tag }))),
          facts,
          el("p", { class: "how", text: texts.inApp }),
          installRow(item, texts),
        ]),
      ]));
      history.replaceState(null, "", `#${encodeURIComponent(item.id)}`);
      if (!dialog.open) dialog.showModal();
      closeButton.focus({ preventScroll: true });
    };
    return { show, close };
  }

  // ---- Marketplace and galleries ----------------------------------------------------------------------------------

  function readTexts(gallery) {
    const d = gallery.dataset;
    return {
      copy: d.copy, copied: d.copied, by: d.by, permissions: d.permissions, noPermission: d.noPermission,
      search: d.search, empty: d.empty, error: d.error, all: d.all, details: d.details, close: d.close,
      version: d.version, inApp: d.inApp, updated: d.updated, count: d.count, moreTags: d.moreTags,
      kinds: { theme: d.kindTheme, snippet: d.kindSnippet, extension: d.kindExtension, app: d.kindApp },
      kindsPlural: { theme: d.pluralTheme, snippet: d.pluralSnippet, extension: d.pluralExtension, app: d.pluralApp },
    };
  }

  function matches(item, state) {
    if (state.kinds.length && !state.kinds.includes(item.kind)) return false;
    if (state.tag && !tagsOf(item).includes(state.tag)) return false;
    if (!state.query) return true;
    const haystack = [item.name, item.id, asText(item.description), ...tagsOf(item)].join(" ").toLowerCase();
    return haystack.includes(state.query);
  }

  // Tabs over the kinds (only on the Marketplace page); a pill slides under the chosen one.
  function renderTabs(texts, allKinds, onPick) {
    const list = el("div", { class: "tabs", role: "tablist", "aria-label": texts.all });
    const pill = el("span", { class: "tab-pill", "aria-hidden": "true" });
    const choices = [{ key: "", label: texts.all }, ...allKinds.map((k) => ({ key: k, label: texts.kindsPlural[k] }))];
    const buttons = choices.map((choice, i) => {
      const button = el("button", { type: "button", role: "tab", "aria-selected": String(i === 0), "data-kind": choice.key },
        [el("span", { text: choice.label }), el("span", { class: "n" })]);
      button.addEventListener("click", () => onPick(choice.key));
      return button;
    });
    list.append(pill, ...buttons);
    const movePill = () => {
      const on = buttons.find((b) => b.getAttribute("aria-selected") === "true");
      if (!on) return;
      pill.style.width = `${on.offsetWidth}px`;
      pill.style.transform = `translateX(${on.offsetLeft}px)`;
    };
    new ResizeObserver(movePill).observe(list);
    return {
      node: list,
      select(key) {
        buttons.forEach((b) => b.setAttribute("aria-selected", String(b.dataset.kind === key)));
        movePill();
      },
      counts(items) {
        buttons.forEach((b) => {
          const n = b.dataset.kind ? items.filter((it) => it.kind === b.dataset.kind).length : items.length;
          b.querySelector(".n").textContent = String(n);
        });
        movePill();
      },
    };
  }

  function renderTagRow(texts, onPick) {
    const row = el("div", { class: "tag-row", role: "group" });
    let current = "";
    return {
      node: row,
      fill(tags, chosen) {
        current = chosen;
        row.replaceChildren(...tags.map((tag) => {
          const button = el("button", { type: "button", "aria-pressed": String(tag === current), text: tag });
          button.addEventListener("click", () => onPick(tag === current ? "" : tag));
          return button;
        }));
      },
    };
  }

  async function initGallery(gallery) {
    const status = gallery.querySelector(".status");
    const texts = readTexts(gallery);
    const fixedKinds = (gallery.dataset.kinds || "").split(",").filter((k) => KINDS.includes(k));
    const withTabs = gallery.hasAttribute("data-tabs");
    const state = { kinds: fixedKinds, tag: "", query: "" };
    const dialog = makeDialog(texts);
    let items = [];
    let lastText = "";

    const search = el("input", { type: "search", placeholder: texts.search, "aria-label": texts.search, autocomplete: "off" });
    const tagRow = renderTagRow(texts, (tag) => { state.tag = tag; draw(); });
    const tabs = withTabs ? renderTabs(texts, KINDS, (kind) => {
      state.kinds = kind ? [kind] : [];
      state.tag = "";
      tabs.select(kind);
      draw();
    }) : null;
    const grid = el("div", { class: "grid", "aria-live": "polite" });
    const count = el("p", { class: "count" });
    const bar = el("div", { class: "filters" }, [tabs ? tabs.node : null, el("div", { class: "search-row" }, [search, count]), tagRow.node]);
    search.addEventListener("input", () => { state.query = search.value.trim().toLowerCase(); draw(); });

    const open = (item, from) => dialog.show(item, from);

    function draw() {
      const inKind = items.filter((item) => !state.kinds.length || state.kinds.includes(item.kind));
      const tags = [...new Set(inKind.flatMap(tagsOf))].sort();
      if (state.tag && !tags.includes(state.tag)) state.tag = "";
      tagRow.fill(tags, state.tag);
      const shown = items.filter((item) => matches(item, state));
      grid.replaceChildren(...shown.map((item, i) => {
        const node = card(item, texts, i, open);
        if (!reducedMotion.matches) node.style.setProperty("--delay", `${Math.min(i, STAGGER_MAX) * STAGGER_MS}ms`);
        return node;
      }));
      count.textContent = texts.count.replace("{n}", String(shown.length));
      status.textContent = shown.length ? "" : texts.empty;
      status.hidden = shown.length > 0;
    }

    function openFromHash() {
      const id = decodeURIComponent(location.hash.slice(1));
      const item = id && items.find((it) => it.id === id);
      if (item) dialog.show(item, null);
    }

    async function load(isRefresh) {
      try {
        const result = await readCatalog();
        if (result.text === lastText) return;
        const first = !lastText;
        lastText = result.text;
        items = result.items;
        if (tabs) tabs.counts(items);
        draw();
        if (first) {
          status.after(bar, grid);
          openFromHash();
        } else if (isRefresh) {
          status.hidden = false;
          status.textContent = texts.updated;
          setTimeout(() => { if (status.textContent === texts.updated) status.hidden = true; }, 4000);
        }
      } catch {
        if (!lastText) status.textContent = texts.error;
      }
    }

    // The catalog changes when an element is published: read it again every few minutes and when the tab comes back.
    let timer = 0;
    const schedule = () => {
      clearInterval(timer);
      if (!document.hidden) timer = setInterval(() => load(true), REFRESH_MS);
    };
    document.addEventListener("visibilitychange", () => {
      if (!document.hidden && lastText) load(true);
      schedule();
    });
    window.addEventListener("hashchange", openFromHash);
    await load(false);
    schedule();
  }

  // The home page's strip: a few elements of the live catalog with a picture, taken in turn from each kind the hero
  // does not already show.
  async function initStrip(strip) {
    const STRIP_SIZE = 6;
    try {
      const { items } = await readCatalog();
      const queues = ["snippet", "extension", "app"].map((kind) => items.filter((it) => it.kind === kind && isImage(it.preview)));
      const picks = [];
      for (let i = 0; picks.length < STRIP_SIZE && queues.some((q) => q.length); i++) {
        const queue = queues[i % queues.length];
        if (queue.length) picks.push(queue.shift());
      }
      const base = strip.dataset.market;
      strip.replaceChildren(...picks.map((item, i) => {
        const link = el("a", { class: "strip-item", href: `${base}#${encodeURIComponent(item.id)}` }, [
          shot(item, false),
          el("span", { class: "strip-name", text: item.name }),
        ]);
        reveal(link, i);
        return link;
      }));
      const total = strip.parentElement.querySelector("[data-total]");
      if (total) total.textContent = total.dataset.total.replace("{n}", String(items.length));
    } catch {
      strip.hidden = true;
    }
  }

  document.addEventListener("DOMContentLoaded", () => {
    initCopyButtons();
    initRepoLinks();
    initReveal();
    document.querySelectorAll("[data-rotate]").forEach(initRotator);
    document.querySelectorAll("[data-gallery]").forEach(initGallery);
    document.querySelectorAll("[data-strip]").forEach(initStrip);
    initSmoothScroll();
  });
})();
