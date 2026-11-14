// Shift-click a drift name in the book's code to open its reference entry.
//
// The names come from reference/api-index.json, which
// scripts/candela-reference.py writes next to the reference pages. Every
// name in a code block or an inline code span that the index knows becomes
// a span; hovering one shows its signature, Shift underlines them all,
// Shift-click opens the entry and Ctrl/Cmd+Shift-click opens it in a new
// tab. A plain click is left alone, so selecting text works as usual.
(function () {
    "use strict";

    // mdBook sets path_to_root on every page; it is relative ("../") except
    // on the 404 page, so the book works under any base path.
    const root = new URL(typeof path_to_root === "string" ? path_to_root : "", document.baseURI);
    const IDENT = /@?[A-Za-z_][A-Za-z0-9_]*/g;
    const BINDERS = /(?:\b(?:fn|let|mut|struct|enum|const|for)\s+)$/;

    let index = null;

    // ---- Reading types out of the code ------------------------------------

    // The capitalised names in a type such as `Lookup<Health>` or
    // `Res<Time>`, outermost first.
    function typeNames(text) {
        return (text.match(/[A-Z][A-Za-z0-9_]*/g) || []);
    }

    // The type text that starts at text[i], up to a `,`, `)`, `=`, `{` or
    // `;` outside angle brackets.
    function readType(text, i) {
        let depth = 0;
        let j = i;
        for (; j < text.length; j++) {
            const c = text[j];
            if (c === "<" || c === "[" || c === "(") depth++;
            else if (c === ">" || c === "]" || (c === ")" && depth > 0)) depth--;
            else if (depth === 0 && ",)={;\n".includes(c)) break;
        }
        return text.slice(i, j);
    }

    function returnTypes(entry) {
        const m = entry && entry.sig.match(/->\s*(.+)$/);
        return m ? typeNames(m[1]) : [];
    }

    // Names bound in a piece of code, and the types they hold: parameters
    // and fields (`hit: Contact<A, B>`), and `let x = Type...` or
    // `let x = function(...)`.
    function bindings(text, into, modules) {
        let m;
        const typed = /\b([a-z_][A-Za-z0-9_]*)\s*:(?!:)\s*/g;
        while ((m = typed.exec(text))) {
            const names = typeNames(readType(text, typed.lastIndex));
            if (names.length) into[m[1]] = names;
        }
        const assigned = /\blet\s+(?:mut\s+)?([a-z_][A-Za-z0-9_]*)\s*=\s*([A-Za-z_][A-Za-z0-9_]*)/g;
        while ((m = assigned.exec(text))) {
            if (into[m[1]]) continue;
            if (/^[A-Z]/.test(m[2])) into[m[1]] = [m[2]];
            else {
                const ret = returnTypes(pick(m[2], modules));
                if (ret.length) into[m[1]] = ret;
            }
        }
        return into;
    }

    // Names a piece of code declares itself (its own structs, enums, their
    // variants and its functions), and the local names it binds. A game's
    // `enum Action { Wander }` or a parameter called `health` is not the
    // drift name it shares.
    function declared(text, into) {
        let m;
        const types = /\b(struct|enum|fn)\s+([A-Za-z_][A-Za-z0-9_]*)/g;
        while ((m = types.exec(text))) {
            into.add(m[2]);
            if (m[1] === "enum") {
                const open = text.indexOf("{", types.lastIndex);
                const close = text.indexOf("}", open);
                if (open >= 0 && close > open) {
                    for (const v of text.slice(open + 1, close).match(/[A-Za-z_][A-Za-z0-9_]*/g) || []) into.add(v);
                }
            }
        }
        return into;
    }

    function bound(text, into) {
        let m;
        const re = /\b([a-z_][A-Za-z0-9_]*)\s*:(?!:)|\blet\s+(?:mut\s+)?([a-z_][A-Za-z0-9_]*)|\bfor\s+\(?([a-z_][A-Za-z0-9_, ]*)\)?\s+in\b|\|([a-z_][A-Za-z0-9_, ]*)\|/g;
        while ((m = re.exec(text))) {
            for (const name of (m[1] || m[2] || m[3] || m[4]).split(",")) {
                if (name.trim()) into.add(name.trim());
            }
        }
        return into;
    }

    function imports(text) {
        const out = [];
        const re = /import\s+"([^"]+)"/g;
        let m;
        while ((m = re.exec(text))) out.push(m[1]);
        return out;
    }

    // The entry for a name: the one from a module the page imports, else the
    // one from the core drift module. A name from drift/ui and the other
    // opt-in modules only counts on a page that imports its module, so a
    // Joint's `anchor` field is not taken for drift/ui's `anchor()`.
    function pick(name, modules) {
        const entries = index[name];
        if (!entries) return null;
        return entries.find((e) => modules.includes(e.module))
            || entries.find((e) => e.module === "drift")
            || null;
    }

    // The types the expression that ends just before text[end] can have:
    // a type name, a bound name, a field of either, or a call's result.
    function typesBefore(text, end, ctx, depth) {
        if (depth > 6) return [];
        let i = end;
        while (i > 0 && /\s/.test(text[i - 1])) i--;
        if (text[i - 1] === ")") {
            let level = 0;
            let j = i - 1;
            for (; j >= 0; j--) {
                if (text[j] === ")") level++;
                else if (text[j] === "(") { level--; if (level === 0) break; }
            }
            if (j < 0) return [];
            // A generic call such as `choice<Action>()`.
            if (text[j - 1] === ">") {
                let angle = 0;
                for (j--; j >= 0; j--) {
                    if (text[j] === ">") angle++;
                    else if (text[j] === "<" && --angle === 0) break;
                }
                if (j < 0) return [];
            }
            const callee = identBefore(text, j);
            if (!callee) return [];
            const entry = resolve(text, callee.start, callee.name, ctx, depth + 1);
            return entry ? returnTypes(entry) : [];
        }
        const id = identBefore(text, i);
        if (!id) return [];
        if (/^[A-Z]/.test(id.name)) return [id.name];
        if (text[id.start - 1] === ".") {
            const owners = typesBefore(text, id.start - 1, ctx, depth + 1);
            for (const t of owners) {
                const entry = pick(t, ctx.modules);
                if (entry && entry.fields && entry.fields[id.name]) return typeNames(entry.fields[id.name]);
            }
        }
        // A bound name, or a field of the game's own struct declared in the code.
        return ctx.local[id.name] || ctx.page[id.name] || [];
    }

    function identBefore(text, end) {
        let i = end;
        while (i > 0 && /\s/.test(text[i - 1])) i--;
        let s = i;
        while (s > 0 && /[A-Za-z0-9_]/.test(text[s - 1])) s--;
        if (s === i || /[0-9]/.test(text[s])) return null;
        return { name: text.slice(s, i), start: s };
    }

    // The index entry the identifier at text[start] names, or null.
    function resolve(text, start, name, ctx, depth) {
        if (name[0] === "@") return pick(name, ctx.modules);
        let before = start;
        while (before > 0 && / /.test(text[before - 1])) before--;
        // A path such as `Schedule::Startup`.
        if (text.slice(before - 2, before) === "::") {
            const left = identBefore(text, before - 2);
            return left ? pick(left.name + "::" + name, ctx.modules) : null;
        }
        // A method: `Type.method`, `x.method`, `a.field.method`, `f().method`.
        if (text[before - 1] === ".") {
            for (const t of typesBefore(text, before - 1, ctx, depth)) {
                const entry = pick(t + "." + name, ctx.modules);
                if (entry) return entry;
            }
            return null;
        }
        // A name being bound or declared, or a field written `name: value`,
        // is not a use of the drift name it may share.
        if (BINDERS.test(text.slice(Math.max(0, start - 12), start))) return null;
        const after = text.slice(start + name.length, start + name.length + 8);
        if (/^\s*:(?!:)/.test(after)) return null;
        if (ctx.declared.has(name)) return null;
        if (!/^\s*[(<]/.test(after) && ctx.bound.has(name)) return null;
        return pick(name, ctx.modules);
    }

    // ---- Marking the names -------------------------------------------------

    function textNodes(el) {
        const out = [];
        const walker = document.createTreeWalker(el, NodeFilter.SHOW_TEXT);
        let pos = 0;
        let node;
        while ((node = walker.nextNode())) {
            out.push({ node, start: pos, end: pos + node.data.length });
            pos += node.data.length;
        }
        return out;
    }

    function wrap(node, a, b, entry, name) {
        let mid = a > 0 ? node.splitText(a) : node;
        if (b - a < mid.data.length) mid.splitText(b - a);
        const span = document.createElement("span");
        span.className = "api-ref";
        span.dataset.apiName = name;
        span.dataset.apiUrl = new URL(entry.url, root).href;
        span.dataset.apiSig = entry.sig;
        span.dataset.apiModule = entry.module;
        mid.parentNode.replaceChild(span, mid);
        span.appendChild(mid);
    }

    function mark(el, ctx) {
        const text = el.textContent;
        const found = [];
        let m;
        IDENT.lastIndex = 0;
        while ((m = IDENT.exec(text))) {
            const entry = resolve(text, m.index, m[0], ctx, 0);
            if (entry) found.push({ start: m.index, end: m.index + m[0].length, entry, name: m[0] });
        }
        if (!found.length) return;
        const nodes = textNodes(el);
        // Last name first: splitting a text node keeps its earlier part in
        // the original node, so the offsets still to come stay valid.
        for (let k = found.length - 1; k >= 0; k--) {
            const f = found[k];
            for (let n = nodes.length - 1; n >= 0; n--) {
                const t = nodes[n];
                if (t.end <= f.start || t.start >= f.end) continue;
                // Words in strings and comments are prose, not uses.
                if (t.node.parentElement.closest("a, .api-ref, .hljs-string, .hljs-comment")) continue;
                wrap(t.node, Math.max(f.start, t.start) - t.start, Math.min(f.end, t.end) - t.start, f.entry, f.name);
            }
        }
    }

    // A bare name in a table column of fields or keys, such as the `wrap`
    // row of Text's field table, is a field and not the drift function it
    // shares a name with.
    function isFieldName(code) {
        const cell = code.closest("td");
        if (!cell || !/^[a-z_][A-Za-z0-9_]*$/.test(code.textContent.trim())) return false;
        const table = cell.closest("table");
        const head = table && table.querySelectorAll("thead th")[cell.cellIndex];
        return !!head && /\b(field|key|member)s?\b/i.test(head.textContent);
    }

    function markPage() {
        const main = document.querySelector("#mdbook-content main") || document.querySelector("main");
        if (!main) return;
        const blocks = Array.from(main.querySelectorAll("pre code"));
        const inline = Array.from(main.querySelectorAll("code")).filter((c) => !c.closest("pre, a, h1, h2, h3, h4, h5, h6"));
        const all = blocks.map((b) => b.textContent).join("\n");
        const page = bindings(all, {}, imports(all));
        const pageModules = imports(all);
        const pageDeclared = declared(all, new Set());
        const pageBound = bound(all, new Set());
        for (const block of blocks) {
            const text = block.textContent;
            const own = imports(text);
            mark(block, {
                local: bindings(text, {}, own.concat(pageModules)), page, modules: own.concat(pageModules),
                declared: declared(text, new Set()), bound: bound(text, new Set()),
            });
        }
        for (const code of inline) {
            if (isFieldName(code)) continue;
            mark(code, { local: {}, page, modules: pageModules, declared: pageDeclared, bound: pageBound });
        }
    }

    // ---- Shift, clicks and the tooltip -----------------------------------

    function arm(on) {
        document.documentElement.classList.toggle("api-links-armed", on);
    }

    function refAt(event) {
        return event.target instanceof Element ? event.target.closest(".api-ref") : null;
    }

    document.addEventListener("keydown", (e) => { if (e.key === "Shift") arm(true); });
    document.addEventListener("keyup", (e) => { if (e.key === "Shift") arm(false); });
    window.addEventListener("blur", () => arm(false));
    document.addEventListener("mousemove", (e) => arm(e.shiftKey));

    // Shift on mouse down would extend the text selection to the name.
    document.addEventListener("mousedown", (e) => {
        if (e.button === 0 && e.shiftKey && refAt(e)) e.preventDefault();
    }, true);

    document.addEventListener("click", (e) => {
        const ref = refAt(e);
        if (!ref || e.button !== 0 || !e.shiftKey) return;
        e.preventDefault();
        e.stopPropagation();
        hideTip();
        if (e.ctrlKey || e.metaKey) window.open(ref.dataset.apiUrl, "_blank", "noopener");
        else window.location.href = ref.dataset.apiUrl;
    }, true);

    let tip = null;
    let tipTimer = 0;

    function showTip(ref) {
        if (!tip) {
            tip = document.createElement("div");
            tip.className = "api-tip";
            tip.setAttribute("role", "tooltip");
            tip.innerHTML = '<code class="api-tip-sig"></code><div class="api-tip-hint"></div>';
            document.body.appendChild(tip);
        }
        tip.querySelector(".api-tip-sig").textContent = ref.dataset.apiSig;
        tip.querySelector(".api-tip-hint").textContent =
            ref.dataset.apiModule + " - Shift-click to open, Ctrl+Shift-click for a new tab";
        tip.style.display = "block";
        const r = ref.getBoundingClientRect();
        const w = tip.offsetWidth;
        const h = tip.offsetHeight;
        const left = Math.max(4, Math.min(r.left, window.innerWidth - w - 4));
        const top = r.bottom + 6 + h > window.innerHeight ? r.top - h - 6 : r.bottom + 6;
        tip.style.left = left + "px";
        tip.style.top = top + "px";
    }

    function hideTip() {
        clearTimeout(tipTimer);
        if (tip) tip.style.display = "none";
    }

    document.addEventListener("mouseover", (e) => {
        const ref = refAt(e);
        if (!ref) return;
        clearTimeout(tipTimer);
        tipTimer = setTimeout(() => showTip(ref), 250);
    });
    document.addEventListener("mouseout", (e) => {
        const ref = refAt(e);
        if (ref && !ref.contains(e.relatedTarget)) hideTip();
    });
    window.addEventListener("scroll", hideTip, true);

    // ---- Start ---------------------------------------------------------

    const ready = new Promise((resolve) => {
        if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", resolve);
        else resolve();
    });
    // A page opened as `...?name=BodyKind::Kinematic` goes on to that name's
    // entry. The editor's "Open docs" link is built this way from the `docs`
    // URL in bindings/candela/drift/candela.toml. A field or a variant
    // without an entry of its own opens its type's (`RigidBody.kind` opens
    // RigidBody); a name the index does not know stays on this page.
    function openByName() {
        let name = new URLSearchParams(window.location.search).get("name");
        while (name) {
            const entries = index[name];
            const entry = entries && (entries.find((e) => e.module === "drift") || entries[0]);
            if (entry) {
                window.location.replace(new URL(entry.url, root));
                return true;
            }
            const shorter = name.replace(/(?:\.|::)[A-Za-z0-9_]+$/, "");
            name = shorter === name ? "" : shorter;
        }
        return false;
    }

    fetch(new URL("reference/api-index.json", root))
        .then((r) => (r.ok ? r.json() : Promise.reject(new Error(r.status))))
        .then((data) => ready.then(() => { index = data; if (!openByName()) markPage(); }))
        .catch((err) => console.warn("api-links: no API index:", err));
})();
