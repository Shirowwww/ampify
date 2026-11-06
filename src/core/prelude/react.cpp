// Module react: Ampify.React, Ampify.ReactDOM, Ampify.hooks, Ampify.components and Ampify.ready. Natives: src/core/natives/react.cpp.
//
// Spicetify hands React to its extensions. A native app cannot run the real React, so this is a small engine that
// accepts the same way of writing (createElement or h, fragments, keys, function components, the basic hooks,
// contexts) and renders into the primitive trees of core/ui_model, through Ampify.ui. Event props (onClick, onChange)
// become callback ids that the host reports with ScriptExtension::DeliverClick or DeliverEvent("change", ...). A
// state change re-renders the whole root once, at the end of the current kernel call (microtask), so several
// setState calls in one handler give one tree. Effects run after the tree was sent, and their cleanups run on
// unmount and when the element stops.
//
// The text is cut in several literals because MSVC refuses a single string literal over 16 KB.

namespace ampify::prelude {

extern const char kReact[];
const char kReact[] = R"AMPIFYJS(
  // ---- Elements ---------------------------------------------------------------------------------------------

  const ELEMENT = Symbol("Ampify.element");
  const Fragment = Symbol("Ampify.Fragment");
  const PROVIDER = Symbol("Ampify.Provider");
  const CONSUMER = Symbol("Ampify.Consumer");

  const isValidElement = (value) => typeof value === "object" && value !== null && value.$$typeof === ELEMENT;

  const isElementType = (type) =>
    typeof type === "string" || typeof type === "function" || type === Fragment ||
    (typeof type === "object" && type !== null && (type.$$typeof === PROVIDER || type.$$typeof === CONSUMER));

  function makeElement(type, key, props, config, children) {
    if (config !== null && config !== undefined) {
      for (const name of Object.keys(config)) {
        if (name === "key") key = config.key === undefined || config.key === null ? null : String(config.key);
        else props[name] = config[name];
      }
    }
    if (children.length === 1) props.children = children[0];
    else if (children.length > 1) props.children = children;
    return Object.freeze({ $$typeof: ELEMENT, type, key, props: Object.freeze(props) });
  }

  function createElement(type, config, ...children) {
    if (!isElementType(type)) throw new TypeError("createElement: invalid element type (" + describe(type) + ")");
    return makeElement(type, null, {}, config, children);
  }

  function cloneElement(element, config, ...children) {
    if (!isValidElement(element)) throw new TypeError("cloneElement expects an element");
    return makeElement(element.type, element.key, Object.assign({}, element.props), config, children);
  }

  const childList = (children) => (children === undefined ? [] : Array.isArray(children) ? children : [children]);

  function flatten(children, out) {
    for (const child of childList(children)) {
      if (Array.isArray(child)) flatten(child, out);
      else if (child !== null && child !== undefined && typeof child !== "boolean") out.push(child);
    }
    return out;
  }

  const Children = Object.freeze({
    toArray: (children) => flatten(children, []),
    map: (children, fn) => flatten(children, []).map((child, index) => fn(child, index)),
    forEach(children, fn) {
      flatten(children, []).forEach((child, index) => fn(child, index));
    },
    count: (children) => flatten(children, []).length,
    only(children) {
      if (!isValidElement(children)) throw new TypeError("Children.only expects a single element");
      return children;
    },
  });

  function createContext(defaultValue) {
    const context = { defaultValue };
    context.Provider = Object.freeze({ $$typeof: PROVIDER, context });
    context.Consumer = Object.freeze({ $$typeof: CONSUMER, context });
    return Object.freeze(context);
  }

  // No memoization: a whole root renders again on each change, which stays small (300 primitives at most).
  const memo = (component) => component;

  // ---- Hooks ------------------------------------------------------------------------------------------------

  let rendering = null;  // the component instance being rendered
  let hookIndex = 0;
  const contextValues = new Map();  // context -> stack of the Provider values above the component being rendered

  function hookSlot(name) {
    if (!rendering) throw new Error(name + ": a hook can only be called while a component renders");
    const hooks = rendering.hooks;
    if (hookIndex >= hooks.length) hooks.push({});
    return hooks[hookIndex++];
  }

  const noDeps = (deps) => deps === undefined || deps === null;
  const sameDeps = (a, b) =>
    Array.isArray(a) && Array.isArray(b) && a.length === b.length && a.every((value, i) => Object.is(value, b[i]));

  function useReducer(reducer, initialArg, init) {
    const instance = rendering;
    const hook = hookSlot("useReducer");
    if (!hook.dispatch) {
      hook.state = init === undefined ? initialArg : init(initialArg);
      hook.dispatch = (action) => {
        if (instance.dead) return;
        const next = hook.reducer(hook.state, action);
        if (Object.is(next, hook.state)) return;
        hook.state = next;
        schedule(instance.root);
      };
    }
    hook.reducer = reducer;
    return [hook.state, hook.dispatch];
  }

  const setStateReducer = (state, action) => (typeof action === "function" ? action(state) : action);
  const useState = (initial) =>
    useReducer(setStateReducer, initial, (value) => (typeof value === "function" ? value() : value));

  function useEffect(effect, deps) {
    if (typeof effect !== "function") throw new TypeError("useEffect expects a function");
    const instance = rendering;
    const hook = hookSlot("useEffect");
    if (hook.ran && !noDeps(deps) && sameDeps(hook.deps, deps)) return;
    hook.effect = effect;
    hook.nextDeps = deps;
    instance.effects.push(hook);
  }

  function useMemo(compute, deps) {
    const hook = hookSlot("useMemo");
    if (!hook.computed || noDeps(deps) || !sameDeps(hook.deps, deps)) {
      hook.value = compute();
      hook.deps = deps;
      hook.computed = true;
    }
    return hook.value;
  }

  const useCallback = (fn, deps) => useMemo(() => fn, deps);

  function useRef(initial) {
    const hook = hookSlot("useRef");
    if (!hook.ref) hook.ref = Object.seal({ current: initial });
    return hook.ref;
  }

  function readContext(context) {
    const stack = contextValues.get(context);
    return stack && stack.length ? stack[stack.length - 1] : context.defaultValue;
  }

  function useContext(context) {
    if (!rendering) throw new Error("useContext: a hook can only be called while a component renders");
    if (typeof context !== "object" || context === null || !context.Provider) {
      throw new TypeError("useContext expects a context made by createContext");
    }
    return readContext(context);
  }

  // The player state, kept up to date by its events (permission player.read). One subscription for the element.
  const playerSetters = new Set();
  let playerSubscribed = false;

  function usePlayer() {
    const [state, setState] = useState(() => Ampify.player.state());
    useEffect(() => {
      if (!playerSubscribed) {
        playerSubscribed = true;
        const push = (next) => {
          for (const set of playerSetters) set(next);
        };
        Ampify.player.on("trackchange", push);
        Ampify.player.on("statechange", push);
      }
      playerSetters.add(setState);
      return () => playerSetters.delete(setState);
    }, []);
    return state;
  }
)AMPIFYJS" R"AMPIFYJS(
  // ---- Rendering ----------------------------------------------------------------------------------------------
  // An instance is kept per position (or key) of the tree, so hooks keep their state between renders. A root is a
  // panel, a page or a named popup; it renders again as a whole.

  const roots = new Map();     // "panel:<id>", "page:<path>" or "popup:<name>" -> root
  const handlers = new Map();  // callback id -> { id, fn }
  const pending = new Set();   // roots to render again at the end of the current kernel call
  let flushQueued = false;
  let busy = false;
  const kMaxPasses = 50;

  // HTML names that Spicetify code often uses, and what they become.
  const ALIASES = { div: "stack", section: "stack", span: "text", p: "text", label: "text", h1: "text", h2: "text",
                    h3: "text", img: "image", select: "dropdown", ul: "list" };
  const VARIANTS = { h1: "title", h2: "subtitle", h3: "subtitle" };
  const CONTAINERS = new Set(["stack", "row", "list", "card", "dropdown", "layer", "grid", "tabs", "menu"]);
  // Where the text children of a primitive without children go.
  const TEXT_ATTRIBUTE = { text: "text", button: "label", option: "label", toggle: "label" };

  function kindOf(child) {
    if (typeof child === "string" || typeof child === "number") return "text";
    if (Array.isArray(child)) return "array";
    if (!isValidElement(child)) {
      throw new TypeError("invalid child: " + describe(child) + " (element, text, number or list expected)");
    }
    const type = child.type;
    if (typeof type === "string") return "host";
    if (typeof type === "function") return "component";
    if (type === Fragment) return "fragment";
    return type.$$typeof === PROVIDER ? "provider" : "consumer";
  }

  // Renders a list of children against the instances of the previous render, matched by key, or else by position.
  // Pushes the primitives (objects) and texts (strings) they produce to `out`. Returns the new instances.
  function reconcile(list, previous, root, out) {
    const next = new Map();
    list.forEach((child, index) => {
      if (child === null || child === undefined || typeof child === "boolean") return;
      let slot = isValidElement(child) && child.key !== null ? "k" + child.key : "i" + index;
      while (next.has(slot)) slot += "+";  // a repeated key falls back to its position
      const kind = kindOf(child);
      const type = kind === "text" || kind === "array" ? kind : child.type;
      let old = previous ? previous.get(slot) : undefined;
      if (old && (old.kind !== kind || old.type !== type)) old = undefined;
      next.set(slot, renderChild(child, kind, type, old, root, out));
    });
    if (previous) {
      for (const [slot, old] of previous) {
        if (next.get(slot) !== old) unmount(old);
      }
    }
    return next;
  }

  // The key of an element goes on the nodes it draws: the kernel then matches the native controls by key too, and a
  // child put before them does not rebuild them. The nodes of a fragment, or of a component that draws several, keep
  // apart by their own key, else by their position.
  function keyNodes(out, start, key, fragment) {
    if (key === null) return;
    const many = fragment || out.length - start > 1;
    for (let index = start; index < out.length; index++) {
      const node = out[index];
      if (typeof node === "string") continue;
      node.key = many ? key + "." + (node.key !== undefined ? node.key : index - start) : key;
    }
  }

  function renderChild(child, kind, type, old, root, out) {
    const instance = old || { kind, type, root, hooks: [], effects: [], children: null, handlers: null, dead: false };
    const start = out.length;
    switch (kind) {
      case "text":
        out.push(String(child));
        break;
      case "array":
        instance.children = reconcile(child, instance.children, root, out);
        break;
      case "fragment":
        instance.children = reconcile(childList(child.props.children), instance.children, root, out);
        break;
      case "host": {
        const inner = [];
        instance.children = reconcile(childList(child.props.children), instance.children, root, inner);
        out.push(hostNode(instance, child.props, inner));
        break;
      }
      case "provider": {
        let stack = contextValues.get(type.context);
        if (!stack) contextValues.set(type.context, (stack = []));
        stack.push(child.props.value);
        try {
          instance.children = reconcile(childList(child.props.children), instance.children, root, out);
        } finally {
          stack.pop();
        }
        break;
      }
      case "consumer": {
        const draw = child.props.children;
        if (typeof draw !== "function") throw new TypeError("Consumer expects a function as its child");
        instance.children = reconcile([draw(readContext(type.context))], instance.children, root, out);
        break;
      }
      default:
        renderComponent(instance, child, root, out);
    }
    if (kind !== "text" && kind !== "array") keyNodes(out, start, child.key, kind === "fragment");
    return instance;
  }

  function renderComponent(instance, element, root, out) {
    const savedInstance = rendering;
    const savedIndex = hookIndex;
    rendering = instance;
    hookIndex = 0;
    instance.effects = [];
    let result;
    try {
      result = element.type(element.props);
    } finally {
      rendering = savedInstance;
      hookIndex = savedIndex;
    }
    instance.children = reconcile([result === undefined ? null : result], instance.children, root, out);
    // After the children: the effects of a child run before those of its parent, as in React.
    for (const hook of instance.effects) root.effects.push(hook);
    instance.effects = [];
  }

  function callbackId(instance, key, fn) {
    if (!instance.handlers) instance.handlers = {};
    let entry = instance.handlers[key];
    if (!entry) {
      entry = { id: native.reactHandlerId(), fn };
      instance.handlers[key] = entry;
      handlers.set(entry.id, entry);
    }
    entry.fn = fn;
    return entry.id;
  }

  // One primitive of core/ui_model: "type" first, the scalar props, then the children. Functions other than
  // onClick and onChange, objects (style) and lists are left out: the tree holds scalars only.
  function hostNode(instance, props, inner) {
    const name = ALIASES[instance.type] || instance.type;
    const node = { type: name };
    if (VARIANTS[instance.type]) node.variant = VARIANTS[instance.type];
    for (const key of Object.keys(props)) {
      if (key === "children") continue;
      const value = props[key];
      if (key === "onClick" || key === "onChange") {
        if (typeof value === "function") node[key] = callbackId(instance, key, value);
        else if (instance.handlers && instance.handlers[key]) instance.handlers[key].fn = null;
        continue;
      }
      const scalar = typeof value === "string" || typeof value === "boolean" ||
                     (typeof value === "number" && Number.isFinite(value));
      if (scalar) node[key] = value;
    }
    if (CONTAINERS.has(name)) {
      node.children = inner.map((part) => (typeof part === "string" ? { type: "text", text: part } : part));
    } else if (inner.length) {
      const attribute = TEXT_ATTRIBUTE[name];
      if (!attribute || inner.some((part) => typeof part !== "string")) {
        throw new TypeError("<" + instance.type + "> " + (attribute ? "only accepts text as its child"
                                                                   : "does not accept children"));
      }
      if (node[attribute] === undefined) node[attribute] = inner.join("");
    }
    return node;
  }

  function unmount(instance) {
    if (instance.dead) return;
    instance.dead = true;
    for (const hook of instance.hooks) {
      if (typeof hook.cleanup === "function") {
        const cleanup = hook.cleanup;
        hook.cleanup = undefined;
        runGuarded("effect cleanup", cleanup, []);
      }
    }
    if (instance.handlers) {
      for (const entry of Object.values(instance.handlers)) handlers.delete(entry.id);
    }
    if (instance.children) {
      for (const child of instance.children.values()) unmount(child);
    }
  }

  // Every cleanup first, then every effect, as React does.
  function runEffects(list) {
    for (const hook of list) {
      if (typeof hook.cleanup === "function") {
        const cleanup = hook.cleanup;
        hook.cleanup = undefined;
        runGuarded("effect cleanup", cleanup, []);
      }
    }
    for (const hook of list) {
      hook.ran = true;
      hook.deps = hook.nextDeps;
      const cleanup = runGuarded("effect", hook.effect, []);
      if (typeof cleanup === "function") hook.cleanup = cleanup;
    }
  }

  const TARGETS = ["panel", "page", "popup", "fullscreen"];

  function targetOf(target) {
    let kind;
    let id;
    if (target === "popup" || target === "fullscreen") {
      kind = target;
      id = "main";
    } else if (typeof target === "string" && target.indexOf(":") > 0) {
      kind = target.slice(0, target.indexOf(":"));
      id = target.slice(target.indexOf(":") + 1);
    } else if (typeof target === "object" && target !== null) {
      kind = TARGETS.find((name) => target[name] !== undefined && target[name] !== null);
      if (kind) id = target[kind] === true ? "main" : text(target[kind]);
    }
    if (kind === "fullscreen") id = "main";  // one fullscreen surface: "fullscreen:" names nothing
    if (!TARGETS.includes(kind) || !id) {
      throw new TypeError('ReactDOM: unknown target (expected "panel:<name>", "page:/<path>", "popup:<name>" or ' +
                          '"fullscreen:")');
    }
    return { kind, id, key: kind + ":" + id };
  }

  function send(target, tree, json) {
    if (target.kind === "panel") Ampify.ui.panel.register(target.id, tree);
    else if (target.kind === "page") Ampify.ui.page.register(target.id, tree);
    else if (target.kind === "fullscreen") Ampify.ui.fullscreen.open(tree);
    else native.reactPopup(target.id, json);
  }

  function performRender(root) {
    if (busy) throw new Error("ReactDOM: a render cannot start another one");
    busy = true;
    const out = [];
    root.effects = [];
    try {
      root.children = reconcile([root.element], root.children, root, out);
    } finally {
      busy = false;
      contextValues.clear();
    }
    const tree = out.length === 1 && typeof out[0] === "object"
      ? out[0]
      : { type: "stack", children: out.map((part) => (typeof part === "string" ? { type: "text", text: part } : part)) };
    const json = JSON.stringify(tree);
    if (json !== root.sent) {
      send(root.target, tree, json);
      root.sent = json;
    }
    const effects = root.effects;
    root.effects = [];
    runEffects(effects);
  }

  function schedule(root) {
    if (root.dead) return;
    pending.add(root);
    if (!flushQueued) {
      flushQueued = true;
      Promise.resolve().then(flush);
    }
  }

  function flush() {
    flushQueued = false;
    for (let pass = 0; pending.size > 0; pass++) {
      if (pass >= kMaxPasses) {
        native.log("[error] React: more than " + kMaxPasses + " renders in a row, a state changes on every render");
        pending.clear();
        break;
      }
      const batch = [...pending];
      pending.clear();
      for (const root of batch) {
        if (root.dead) continue;
        try {
          performRender(root);
        } catch (error) {
          native.log("[error] React render (" + root.target.key + "): " + describe(error));
        }
      }
    }
  }

  function destroy(root) {
    root.dead = true;
    pending.delete(root);
    if (root.offClose) root.offClose();
    if (root.children) {
      for (const child of root.children.values()) unmount(child);
    }
    root.children = null;
  }

  function renderInto(element, target) {
    const where = targetOf(target);
    let root = roots.get(where.key);
    if (!root) {
      root = { target: where, element: null, children: null, effects: [], sent: null, dead: false, offClose: null };
      roots.set(where.key, root);
      // The user closed the fullscreen view (or another element took it): the root unmounts, as unmount() would.
      if (where.kind === "fullscreen") {
        root.offClose = Ampify.ui.fullscreen.onClose(() => {
          if (roots.get(where.key) === root) roots.delete(where.key);
          destroy(root);
        });
      }
      // The same for a panel closed with its Close button: rendered again, it mounts anew.
      if (where.kind === "panel") {
        root.offClose = Ampify.ui.panel.onClose(where.id, () => {
          if (roots.get(where.key) === root) roots.delete(where.key);
          destroy(root);
        });
      }
    }
    root.element = element;
    pending.delete(root);
    performRender(root);
    return null;
  }

  function unmountAt(target) {
    const where = targetOf(target);
    const root = roots.get(where.key);
    if (!root) return false;
    destroy(root);
    roots.delete(where.key);
    if (where.kind === "popup") native.reactClosePopup(where.id);
    else if (where.kind === "fullscreen") Ampify.ui.fullscreen.close();
    else if (where.kind === "panel") native.uiPanelRemove(where.id);
    else send(where, { type: "stack", children: [] }, "");
    return true;
  }

  function createRoot(target) {
    targetOf(target);
    return Object.freeze({
      render: (element) => renderInto(element, target),
      unmount: () => unmountAt(target),
    });
  }

  // The element opens one of its pages: the page mounts anew, as a Spicetify custom app does when the user comes back
  // to it, so its state starts over and its effects run again (a page that reads data on mount shows fresh data).
  dispatch.pageShow = (path) => {
    const root = roots.get("page:" + path);
    if (!root || root.dead || !root.children) return;
    for (const child of root.children.values()) unmount(child);
    root.children = null;
    schedule(root);
  };

  // The element stops: every effect cleanup runs. The interface itself is removed by the module ui.
  Ampify.onStop(() => {
    for (const root of roots.values()) destroy(root);
    roots.clear();
  });

  // Clicks on a tree's callback ids; the other ids are those of the controls of Ampify.ui.
  const controlClick = dispatch.click;
  dispatch.click = (id) => {
    const entry = handlers.get(id);
    if (!entry) {
      if (controlClick) controlClick(id);
      return;
    }
    if (typeof entry.fn === "function") runGuarded("click " + id, entry.fn, [{ type: "click", id }]);
  };

  // A new value from the host, as JSON text: {"id": <callback id>, "value": <boolean, number or text>}.
  dispatch.change = (payload) => {
    let data;
    try {
      data = JSON.parse(payload);
    } catch (error) {
      native.log("[error] change: " + describe(error));
      return;
    }
    const id = Number(data && data.id);
    const entry = handlers.get(id);
    if (!entry || typeof entry.fn !== "function") return;
    const value = data.value;
    runGuarded("change " + id, entry.fn, [{ type: "change", id, value, target: { value, checked: value === true } }]);
  };
)AMPIFYJS" R"AMPIFYJS(
  // ---- Components (Spicetify's ReactComponent) ----------------------------------------------------------------
  // Each one renders primitives. They pass the tooltip props through, so that Tooltip can wrap them.

  const h = createElement;
  const TIP = ["tooltip", "tooltipPlacement", "tooltipDelay"];
  // The style attributes of section 5.5: those of any node, and those of a box (card).
  const PLACE = ["opacity", "scale", "transition", "align", "valign", "grow", "inert"];
  const BOX = ["background", "padding", "cornerRadius", "width", "height", "minWidth", "maxWidth"];

  function pick(props, names) {
    const out = {};
    for (const name of names) {
      if (props[name] !== undefined) out[name] = props[name];
    }
    return out;
  }

  // The handler of a component gets the value itself, then the event.
  function relay(attributes, fn, convert) {
    if (typeof fn === "function") attributes.onChange = (event) => fn(convert(event.value), event);
    return attributes;
  }

  const asText = (value) => (value === undefined || value === null ? "" : String(value));

  function Button(props) {
    const attributes = pick(props, ["label", "icon", "disabled", "variant", "onClick", ...TIP, ...PLACE]);
    return h("button", attributes, attributes.label === undefined ? props.children : undefined);
  }
  const ButtonPrimary = (props) => h(Button, Object.assign({ variant: "primary" }, props));
  const ButtonSecondary = (props) => h(Button, Object.assign({ variant: "secondary" }, props));
  const ButtonTertiary = (props) => h(Button, Object.assign({ variant: "tertiary" }, props));

  function Toggle(props) {
    const attributes = pick(props, ["label", "disabled", ...TIP]);
    attributes.checked = Boolean(props.checked !== undefined ? props.checked : props.value);
    relay(attributes, props.onChange || props.onSelected, (value) => value === true);
    return h("toggle", attributes, attributes.label === undefined ? props.children : undefined);
  }

  function Slider(props) {
    const attributes = pick(props, ["label", "min", "max", "step", "value", "disabled", ...TIP]);
    return h("slider", relay(attributes, props.onChange, Number));
  }

  function TextField(props) {
    const attributes = pick(props, ["label", "value", "placeholder", "disabled", ...TIP]);
    return h("textfield", relay(attributes, props.onChange, asText));
  }

  // options: texts, or { value, label } objects. Without options, the children (h("option", ...)) are used.
  function Dropdown(props) {
    const attributes = relay(pick(props, ["label", "value", "disabled", ...TIP]), props.onChange, asText);
    if (attributes.value !== undefined) attributes.value = asText(attributes.value);
    const options = Array.isArray(props.options)
      ? props.options.map((option) => {
          const entry = typeof option === "object" && option !== null ? option : { value: option };
          const value = asText(entry.value);
          return h("option", { key: value, value, label: entry.label === undefined ? value : asText(entry.label) });
        })
      : props.children;
    return h("dropdown", attributes, options);
  }

  // columns (1 to 12) and gap: the children fill the cells row by row.
  const Grid = (props) => h("grid", pick(props, ["columns", "gap", ...TIP]), props.children);

  // options as Dropdown's: the tab of `value` is selected, and a click on a tab gives its value to onChange.
  function Tabs(props) {
    const current = asText(props.value);
    const tabs = Array.isArray(props.options)
      ? props.options.map((option) => {
          const entry = typeof option === "object" && option !== null ? option : { value: option };
          const value = asText(entry.value);
          const pickTab = () => {
            if (typeof props.onChange === "function") props.onChange(value);
          };
          return h("tab", { key: value, value, label: entry.label === undefined ? value : asText(entry.label),
                            selected: value === current, onClick: pickTab });
        })
      : props.children;
    return h("tabs", pick(props, TIP), tabs);
  }

  // items: { label, disabled, onClick }. The menu's button opens them as a flyout under it.
  function Menu(props) {
    const attributes = pick(props, ["label", "disabled", ...TIP]);
    const items = Array.isArray(props.items)
      ? props.items.map((item, index) => {
          const entry = typeof item === "object" && item !== null ? item : { label: item };
          return h("item", { key: String(index), label: asText(entry.label), disabled: entry.disabled === true,
                             onClick: entry.onClick });
        })
      : props.children;
    return h("menu", attributes, items);
  }

  const Card = (props) => h("card", pick(props, ["title", "subtitle", "onClick", ...TIP, ...PLACE, ...BOX]),
                            props.children);
  const List = (props) => h("list", pick(props, TIP), props.children);

  function Text(props) {
    const attributes = pick(props, ["text", "variant", "size", "weight", "color", "wrap", "maxLines", ...TIP, ...PLACE]);
    return h("text", attributes, attributes.text === undefined ? props.children : undefined);
  }

  // Spicetify's TooltipWrapper (Tippy props): label (or content), placement, delay (or showDelay). It adds the
  // tooltip attributes to each child, which must be a primitive or a component of Ampify.components.
  function Tooltip(props) {
    const label = props.label !== undefined ? props.label : props.content;
    const delay = props.delay !== undefined ? props.delay : props.showDelay;
    const extra = pick({ tooltip: label === undefined ? undefined : asText(label), tooltipPlacement: props.placement,
                         tooltipDelay: delay }, TIP);
    return Children.map(props.children, (child) =>
      isValidElement(child) ? cloneElement(child, extra) : h("text", Object.assign({ text: asText(child) }, extra)));
  }

  // ---- The API ----------------------------------------------------------------------------------------------

  const hooks = { useState, useReducer, useEffect, useLayoutEffect: useEffect, useRef, useMemo, useCallback,
                  useContext };

  Ampify.React = Object.assign({ createElement, h: createElement, Fragment, isValidElement, cloneElement, Children,
                              createContext, memo }, hooks);
  Ampify.ReactDOM = { render: renderInto, unmountComponentAtNode: unmountAt, createRoot };
  Ampify.hooks = Object.assign({ usePlayer }, hooks);
  Ampify.components = { Button, ButtonPrimary, ButtonSecondary, ButtonTertiary, Toggle, Slider, TextField, Dropdown,
                     Grid, Tabs, Menu, Card, List, Text, TextComponent: Text, Tooltip, TooltipWrapper: Tooltip };

  // Ampify.ready resolves when the kernel has started the element: on its first Pump, after main.js ran. An app that
  // defines a global render() function, as a Spicetify custom app does, gets it rendered on its page "/<id>" first.
  let markReady;
  Ampify.ready = new Promise((resolve) => {
    markReady = resolve;
  });

  dispatch.ready = () => {
    const page = native.reactAppPage;
    const appRender = globalThis.render;
    if (page && typeof appRender === "function" && !roots.has("page:" + page)) {
      const App = () => appRender();
      runGuarded("render() of the app", renderInto, [h(App), { page }]);
    }
    markReady();
  };
)AMPIFYJS";

}  // namespace ampify::prelude
