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
