#!/usr/bin/env python3
"""Generate the multilingual manual under docs/.

One shell (CSS, JS, layout) shared by every language; each language supplies
only its section list, UI strings and page bodies.  That keeps the navigation
behaviour identical across translations and means a layout fix lands in all
three at once.

Outputs:
    docs/index.html        Japanese
    docs/en.html           English
    docs/zh.html           Chinese (Simplified)
    docs/assets/site.css
    docs/assets/site.js
    docs/.nojekyll

Usage:  python docs_src/build.py
"""

import html as htmlmod
import importlib.util
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "docs_src"
OUT = ROOT / "docs"

VERSION = "v0.1.0"
REPO = "https://github.com/airpocket-soundman/M5Tab-logic-analyzer"

LANGS = [
    ("ja", "index.html", "日本語"),
    ("en", "en.html", "English"),
    ("zh", "zh.html", "中文"),
]

CSS = """
:root {
  --bg: #0f1216;
  --bg-2: #161b21;
  --bg-3: #1d242c;
  --line: #2b343e;
  --fg: #dde5ec;
  --fg-dim: #8d9aa8;
  --accent: #35c7e0;
  --warn: #ff8f3f;
  --bad: #ff5f56;
  --good: #4ad07a;
  --radius: 8px;
  --top-h: 56px;
  --side-w: 268px;
}
@media (prefers-color-scheme: light) {
  :root {
    --bg: #ffffff; --bg-2: #f4f6f8; --bg-3: #e9edf1; --line: #d3dae1;
    --fg: #182029; --fg-dim: #5b6874; --accent: #0a7f96;
  }
}
* { box-sizing: border-box; }
html, body { margin: 0; padding: 0; height: 100%; }
body {
  background: var(--bg); color: var(--fg);
  font-family: "Helvetica Neue", "Segoe UI", "Hiragino Sans", "Noto Sans JP",
               "Noto Sans SC", "Microsoft YaHei", "Yu Gothic UI", system-ui, sans-serif;
  font-size: 15px; line-height: 1.75;
  -webkit-font-smoothing: antialiased;
}
a { color: var(--accent); }

header {
  position: fixed; inset: 0 0 auto 0; height: var(--top-h); z-index: 30;
  display: flex; align-items: center; gap: 18px;
  padding: 0 18px; background: var(--bg-2);
  border-bottom: 1px solid var(--line);
}
.brand { font-weight: 700; letter-spacing: .02em; white-space: nowrap; }
.brand a { color: inherit; text-decoration: none; }
.brand small { display: block; font-weight: 400; font-size: 11px; color: var(--fg-dim); line-height: 1; }
nav.main { display: flex; gap: 4px; overflow-x: auto; flex: 1; }
nav.main button {
  appearance: none; border: 0; background: transparent; color: var(--fg-dim);
  font: inherit; font-size: 14px; padding: 8px 14px; border-radius: var(--radius);
  cursor: pointer; white-space: nowrap;
}
nav.main button:hover { background: var(--bg-3); color: var(--fg); }
nav.main button[aria-current="true"] { background: var(--accent); color: #06181d; font-weight: 600; }

.langs { display: flex; gap: 2px; background: var(--bg-3); border-radius: var(--radius); padding: 3px; }
.langs a {
  font-size: 12.5px; padding: 4px 10px; border-radius: 5px;
  color: var(--fg-dim); text-decoration: none; white-space: nowrap;
}
.langs a:hover { color: var(--fg); }
.langs a[aria-current="true"] { background: var(--accent); color: #06181d; font-weight: 600; }
header .repo { font-size: 12.5px; color: var(--fg-dim); white-space: nowrap; text-decoration: none; }
#menuToggle { display: none; background: var(--bg-3); color: var(--fg); border: 0;
              border-radius: 6px; padding: 6px 10px; cursor: pointer; }

aside {
  position: fixed; top: var(--top-h); bottom: 0; left: 0; width: var(--side-w);
  overflow-y: auto; padding: 18px 12px 40px;
  background: var(--bg-2); border-right: 1px solid var(--line); z-index: 20;
}
aside .toc-title {
  font-size: 11px; letter-spacing: .12em; text-transform: uppercase;
  color: var(--fg-dim); padding: 0 10px 8px;
}
aside ul { list-style: none; margin: 0; padding: 0; }
aside a {
  display: block; padding: 6px 10px; border-radius: 6px;
  color: var(--fg-dim); text-decoration: none; font-size: 14px;
}
aside a:hover { background: var(--bg-3); color: var(--fg); }
aside a.page { color: var(--fg); font-weight: 600; margin-top: 6px; }
aside a.head { font-size: 13px; padding-left: 24px; }
aside a.active { background: var(--bg-3); color: var(--accent); }
aside a.page.active { background: var(--accent); color: #06181d; }

main {
  margin: var(--top-h) 0 0 var(--side-w);
  padding: 34px clamp(20px, 4vw, 56px) 120px;
  max-width: 1080px;
}
article[hidden] { display: none; }
h1 { font-size: 30px; line-height: 1.3; margin: 0 0 6px; letter-spacing: .01em; }
.lede { color: var(--fg-dim); margin: 0 0 30px; font-size: 15px; }
h2 {
  font-size: 20px; margin: 44px 0 12px; padding-top: 10px;
  border-top: 1px solid var(--line); scroll-margin-top: calc(var(--top-h) + 16px);
}
h3 { font-size: 16px; margin: 26px 0 8px; }
p, li { margin: 0 0 12px; }
ul, ol { padding-left: 22px; }
code {
  font-family: "SFMono-Regular", Consolas, "Liberation Mono", monospace;
  font-size: 13px; background: var(--bg-3); padding: 1px 5px; border-radius: 4px;
}
pre {
  background: var(--bg-2); border: 1px solid var(--line); border-radius: var(--radius);
  padding: 14px 16px; overflow-x: auto; margin: 0 0 18px;
}
pre code { background: none; padding: 0; font-size: 12.5px; line-height: 1.6; }
table { border-collapse: collapse; width: 100%; margin: 0 0 20px; font-size: 14px;
        display: block; overflow-x: auto; }
th, td { border: 1px solid var(--line); padding: 7px 11px; text-align: left; vertical-align: top; }
th { background: var(--bg-2); font-weight: 600; white-space: nowrap; }
td.num, th.num { text-align: right; font-variant-numeric: tabular-nums; }

figure { margin: 0 0 26px; }
figure img {
  width: 100%; max-width: 100%; display: block;
  border: 1px solid var(--line); border-radius: var(--radius); background: #000;
}
figcaption { font-size: 13px; color: var(--fg-dim); margin-top: 8px; }

.note, .warn, .tip {
  border-left: 3px solid var(--accent); background: var(--bg-2);
  padding: 12px 16px; border-radius: 0 var(--radius) var(--radius) 0; margin: 0 0 20px;
}
.warn { border-left-color: var(--bad); }
.tip  { border-left-color: var(--good); }
.note p:last-child, .warn p:last-child, .tip p:last-child { margin-bottom: 0; }
.kbd {
  display: inline-block; border: 1px solid var(--line); background: var(--bg-3);
  border-radius: 5px; padding: 0 6px; font-size: 12px; font-family: monospace;
}
.cards { display: grid; gap: 14px; grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
         margin-bottom: 24px; }
.card { background: var(--bg-2); border: 1px solid var(--line);
        border-radius: var(--radius); padding: 14px 16px; }
.card h3 { margin: 0 0 6px; font-size: 15px; }
.card p { margin: 0; font-size: 13.5px; color: var(--fg-dim); }
.big { font-size: 26px; font-weight: 700; color: var(--accent); font-variant-numeric: tabular-nums; }

@media (max-width: 1000px) {
  :root { --side-w: 0px; }
  #menuToggle { display: inline-block; }
  .brand small { display: none; }
  aside { transform: translateX(-100%); transition: transform .18s ease; width: 268px; }
  body.nav-open aside { transform: none; }
  main { margin-left: 0; }
}
""".strip()

JS = """
(function () {
  const SECTIONS = window.SITE_SECTIONS || [];
  const articles = Array.from(document.querySelectorAll('article[data-page]'));
  const nav = document.getElementById('mainNav');
  const toc = document.getElementById('toc');
  const tocTitle = document.getElementById('tocTitle');
  const main = document.getElementById('content');
  if (!articles.length) return;

  // Give every h2 a stable id so the TOC can link to it.
  articles.forEach(a => {
    a.querySelectorAll('h2').forEach((h, i) => {
      if (!h.id) h.id = a.dataset.page + '-h' + i;
    });
  });

  let currentSection = SECTIONS.length ? SECTIONS[0].id : '';
  let currentPage = articles[0].dataset.page;

  SECTIONS.forEach(s => {
    const b = document.createElement('button');
    b.textContent = s.label;
    b.dataset.section = s.id;
    b.addEventListener('click', () => {
      const first = articles.find(a => a.dataset.section === s.id);
      if (first) go(first.dataset.page);
    });
    nav.appendChild(b);
  });

  function pageOf(id) { return articles.find(a => a.dataset.page === id); }

  function buildToc() {
    toc.innerHTML = '';
    const label = SECTIONS.find(s => s.id === currentSection);
    if (label) tocTitle.textContent = label.label;

    articles.filter(a => a.dataset.section === currentSection).forEach(a => {
      const li = document.createElement('li');
      const link = document.createElement('a');
      link.className = 'page' + (a.dataset.page === currentPage ? ' active' : '');
      link.textContent = a.dataset.title;
      link.href = '#' + a.dataset.page;
      link.addEventListener('click', e => { e.preventDefault(); go(a.dataset.page); });
      li.appendChild(link);
      toc.appendChild(li);

      // Sub-entries only for the page being viewed, so the list stays scannable.
      if (a.dataset.page === currentPage) {
        a.querySelectorAll('h2').forEach(h => {
          const sub = document.createElement('li');
          const sl = document.createElement('a');
          sl.className = 'head';
          sl.textContent = h.textContent;
          sl.href = '#' + a.dataset.page + '/' + h.id;
          sl.dataset.target = h.id;
          sl.addEventListener('click', e => { e.preventDefault(); go(a.dataset.page, h.id); });
          sub.appendChild(sl);
          toc.appendChild(sub);
        });
      }
    });
  }

  function go(pageId, anchor, replace) {
    const page = pageOf(pageId);
    if (!page) return;
    currentPage = pageId;
    currentSection = page.dataset.section;

    articles.forEach(a => { a.hidden = a.dataset.page !== pageId; });
    Array.from(nav.children).forEach(b => {
      b.setAttribute('aria-current', b.dataset.section === currentSection ? 'true' : 'false');
    });
    buildToc();
    document.body.classList.remove('nav-open');
    syncLangLinks();

    const hash = '#' + pageId + (anchor ? '/' + anchor : '');
    if (location.hash !== hash) {
      if (replace) history.replaceState(null, '', hash);
      else history.pushState(null, '', hash);
    }

    if (anchor) {
      const el = document.getElementById(anchor);
      if (el) { el.scrollIntoView({ behavior: 'smooth', block: 'start' }); return; }
    }
    window.scrollTo({ top: 0, behavior: 'auto' });
  }

  // Carry the current page across a language switch: the page ids are shared
  // between translations, so the reader lands on the same topic.
  function syncLangLinks() {
    document.querySelectorAll('.langs a[data-file]').forEach(a => {
      a.href = a.dataset.file + location.hash;
    });
  }

  main.addEventListener('click', e => {
    const a = e.target.closest('a[data-goto]');
    if (!a) return;
    e.preventDefault();
    go(a.dataset.goto);
  });

  function fromHash(replace) {
    const raw = location.hash.replace(/^#/, '');
    if (!raw) { go(articles[0].dataset.page, null, true); return; }
    const [p, h] = raw.split('/');
    if (pageOf(p)) go(p, h, replace);
    else go(articles[0].dataset.page, null, true);
  }

  window.addEventListener('popstate', () => fromHash(true));
  const toggle = document.getElementById('menuToggle');
  if (toggle) toggle.addEventListener('click', () => document.body.classList.toggle('nav-open'));

  const spy = new IntersectionObserver(entries => {
    entries.forEach(en => {
      if (!en.isIntersecting) return;
      toc.querySelectorAll('a.head').forEach(a => {
        a.classList.toggle('active', a.dataset.target === en.target.id);
      });
    });
  }, { rootMargin: '-70px 0px -75% 0px' });
  articles.forEach(a => a.querySelectorAll('h2').forEach(h => spy.observe(h)));

  fromHash(true);
})();
""".strip()


def load(name):
    spec = importlib.util.spec_from_file_location(name, SRC / f"{name}.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def render(mod, filename):
    st = mod.STRINGS
    sections = json.dumps([{"id": i, "label": l} for i, l in mod.SECTIONS],
                          ensure_ascii=False)

    langs = []
    for code, fname, label in LANGS:
        cur = ' aria-current="true"' if fname == filename else ""
        langs.append(f'<a href="{fname}" data-file="{fname}"{cur}>{label}</a>')

    arts = []
    for p in mod.PAGES:
        arts.append(
            f'<article data-page="{p["id"]}" data-section="{p["section"]}" '
            f'data-title="{htmlmod.escape(p["title"], quote=True)}">\n{p["body"]}\n</article>')

    return f"""<!doctype html>
<html lang="{st['html_lang']}">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{htmlmod.escape(st['title'])}</title>
<link rel="stylesheet" href="assets/site.css">
</head>
<body>

<header>
  <button id="menuToggle" aria-label="{htmlmod.escape(st['menu_label'], quote=True)}">&#9776;</button>
  <div class="brand"><a href="{REPO}">{htmlmod.escape(st['brand'])}</a><small>{htmlmod.escape(st['tagline'])}</small></div>
  <nav class="main" id="mainNav"></nav>
  <div class="langs">{''.join(langs)}</div>
  <a class="repo" href="{REPO}">{VERSION} &#183; GitHub</a>
</header>

<aside>
  <div class="toc-title" id="tocTitle">{htmlmod.escape(st['toc'])}</div>
  <ul id="toc"></ul>
</aside>

<main id="content">

{chr(10).join(arts)}

</main>

<script>window.SITE_SECTIONS = {sections};</script>
<script src="assets/site.js"></script>
</body>
</html>
"""


def main():
    (OUT / "assets").mkdir(parents=True, exist_ok=True)
    (OUT / "assets" / "site.css").write_text(CSS + "\n", encoding="utf-8")
    (OUT / "assets" / "site.js").write_text(JS + "\n", encoding="utf-8")
    # Stop GitHub Pages from running the content through Jekyll.
    (OUT / ".nojekyll").write_text("", encoding="utf-8")

    ids = None
    for code, fname, _label in LANGS:
        mod = load(f"content_{code}")
        page_ids = [p["id"] for p in mod.PAGES]
        if ids is None:
            ids = page_ids
        elif page_ids != ids:
            print(f"warning: {code} page ids differ from ja; "
                  f"language switching will not keep the reader on the same page",
                  file=sys.stderr)
            print(f"  ja: {ids}\n  {code}: {page_ids}", file=sys.stderr)
        (OUT / fname).write_text(render(mod, fname), encoding="utf-8")
        print(f"wrote docs/{fname}  ({len(mod.PAGES)} pages)")

    print("wrote docs/assets/site.css, docs/assets/site.js, docs/.nojekyll")
    return 0


if __name__ == "__main__":
    sys.exit(main())
