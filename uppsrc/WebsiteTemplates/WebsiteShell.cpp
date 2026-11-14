#include "WebsiteTemplates.h"

NAMESPACE_UPP

// The admin shell. Own styling: the layout pattern is common to admin dashboards, the CSS, icons
// and markup here are written for this package.

static String ShellEsc(const String& value)
{
	StringBuffer out;
	for(int i = 0; i < value.GetCount(); i++) {
		switch(value[i]) {
		case '&': out << "&amp;"; break;
		case '<': out << "&lt;"; break;
		case '>': out << "&gt;"; break;
		case '"': out << "&quot;"; break;
		case '\'': out << "&#39;"; break;
		default: out.Cat(value[i]);
		}
	}
	return String(out);
}

// Inline icons: 24-unit outline paths, drawn with currentColor.
static String ShellIcon(const String& id)
{
	String path;
	if(id == "home")        path = "M3 11l9-7 9 7v9h-6v-6H9v6H3z";
	else if(id == "map")    path = "M9 4L3 6v14l6-2 6 2 6-2V4l-6 2-6-2zM9 4v14M15 6v14";
	else if(id == "list")   path = "M8 6h13M8 12h13M8 18h13M3 6h.01M3 12h.01M3 18h.01";
	else if(id == "clock")  path = "M12 3a9 9 0 100 18 9 9 0 000-18zM12 7v5l3 2";
	else if(id == "sliders")path = "M4 6h10M18 6h2M4 12h4M12 12h8M4 18h12M20 18h0M14 4v4M8 10v4M16 16v4";
	else if(id == "check")  path = "M4 12l5 5L20 6";
	else if(id == "alert")  path = "M12 3l10 18H2zM12 10v5M12 18h.01";
	else return String();
	return "<svg class=\"ad-icon\" viewBox=\"0 0 24 24\" aria-hidden=\"true\" focusable=\"false\"><path d=\"" + path + "\"/></svg>";
}

String AdminDashboardCss()
{
	return R"ADC(
*, *::before, *::after { box-sizing: border-box; }
body.ad-body { margin: 0; min-height: 100vh; background: var(--bg); color: var(--fg); font: 15px/1.5 system-ui, -apple-system, "Segoe UI", sans-serif; }
a { color: var(--accent); text-decoration: none; }
a:hover { text-decoration: underline; }
h1, h2, h3 { line-height: 1.25; margin: 0; }
h1 { font-size: 1.5rem; font-weight: 650; }
h2 { font-size: 1.1rem; font-weight: 650; }
.ad-shell { display: grid; grid-template-columns: 228px minmax(0, 1fr); min-height: 100vh; }
html[data-nav="collapsed"] .ad-shell { grid-template-columns: 60px minmax(0, 1fr); }
.ad-sidebar { position: sticky; top: 0; align-self: start; height: 100vh; overflow: hidden; background: var(--panel); border-right: 1px solid var(--line); padding: 14px 10px; }
.ad-brand { display: block; font-weight: 700; font-size: 1.1rem; color: var(--fg); padding: 4px 8px 16px; white-space: nowrap; }
.ad-sidebar ul { list-style: none; margin: 0; padding: 0; }
.ad-link { display: flex; align-items: center; gap: 10px; padding: 8px 10px; border-radius: 6px; color: var(--muted); white-space: nowrap; }
.ad-link:hover { background: var(--control); color: var(--fg); text-decoration: none; }
.ad-link.is-active { background: var(--sea); color: var(--fg); font-weight: 600; }
.ad-icon { width: 18px; height: 18px; flex: 0 0 18px; fill: none; stroke: currentColor; stroke-width: 1.8; stroke-linecap: round; stroke-linejoin: round; }
html[data-nav="collapsed"] .ad-sidebar { padding: 14px 6px; }
html[data-nav="collapsed"] .ad-brand, html[data-nav="collapsed"] .ad-label { display: none; }
html[data-nav="collapsed"] .ad-link { justify-content: center; }
.ad-main { min-width: 0; display: flex; flex-direction: column; }
.ad-topbar { position: sticky; top: 0; z-index: 5; display: flex; align-items: center; gap: 12px; padding: 8px 16px; background: var(--panel); border-bottom: 1px solid var(--line); }
.ad-toggle { appearance: none; background: transparent; color: var(--fg); border: 1px solid var(--line); border-radius: 6px; width: 34px; height: 34px; display: grid; place-items: center; cursor: pointer; }
.ad-topbar-end { margin-left: auto; display: flex; align-items: center; gap: 10px; flex-wrap: wrap; }
.ad-head { padding: 16px 20px 0; }
.ad-crumbs ol { list-style: none; display: flex; flex-wrap: wrap; gap: 6px; margin: 0 0 6px; padding: 0; font-size: .85rem; color: var(--muted); }
.ad-crumbs li + li::before { content: "/"; margin-right: 6px; color: var(--line); }
.ad-content { padding: 8px 20px 32px; min-width: 0; }
.ad-cards { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 12px; margin: 12px 0 16px; }
.ad-card { background: var(--panel); border: 1px solid var(--line); border-radius: 8px; padding: 12px 14px; }
.ad-card .ad-card-value { font-size: 1.6rem; font-weight: 700; line-height: 1.1; }
.ad-card .ad-card-label { color: var(--muted); font-size: .85rem; }
.ad-panel { background: var(--panel); border: 1px solid var(--line); border-radius: 8px; padding: 14px 16px; margin: 0 0 16px; min-width: 0; }
.ad-panel > header { display: flex; align-items: baseline; gap: 10px; flex-wrap: wrap; margin-bottom: 10px; }
.ad-panel > header p { margin: 0; color: var(--muted); font-size: .9rem; }
.ad-table-wrap { overflow-x: auto; }
.ad-table { width: 100%; border-collapse: collapse; font-size: .92rem; }
.ad-table th, .ad-table td { text-align: left; vertical-align: top; padding: 7px 8px; border-bottom: 1px solid var(--line); }
.ad-table th { color: var(--muted); font-weight: 600; font-size: .82rem; }
.ad-pill { display: inline-block; padding: 1px 8px; border-radius: 999px; border: 1px solid var(--line); font-size: .8rem; margin: 1px 4px 1px 0; white-space: nowrap; }
.ad-pill.is-ok { color: var(--st-active); border-color: var(--st-active); }
.ad-pill.is-warn { color: var(--st-hot); border-color: var(--st-hot); }
.ad-pill.is-err { color: var(--st-trouble); border-color: var(--st-trouble); font-weight: 600; }
.ad-pill.is-info { color: var(--st-planned); border-color: var(--st-planned); }
.ad-pill.is-muted { color: var(--muted); }
.ad-form { display: flex; flex-wrap: wrap; gap: 10px; align-items: end; margin-top: 10px; }
.ad-form label { display: flex; flex-direction: column; gap: 3px; font-size: .82rem; color: var(--muted); min-width: 0; }
.ad-form input, .ad-form select, .ad-form textarea { font: inherit; color: var(--fg); background: var(--control); border: 1px solid var(--line); border-radius: 6px; padding: 6px 8px; min-width: 0; }
.ad-form textarea { min-height: 60px; width: min(100%, 420px); }
.ad-form input.ad-wide { width: min(100%, 320px); }
.ad-btn { font: inherit; color: var(--fg); background: var(--control); border: 1px solid var(--line); border-radius: 6px; padding: 6px 12px; cursor: pointer; }
.ad-btn.is-primary { color: #fff; background: var(--accent); border-color: var(--accent); }
.ad-btn.is-danger { color: var(--st-trouble); }
.ad-btn:disabled { opacity: .5; cursor: default; }
.ad-note { color: var(--muted); font-size: .9rem; margin: 6px 0 0; }
.ad-err { color: var(--st-trouble); border: 1px solid var(--st-trouble); border-radius: 6px; padding: 6px 10px; }
.ad-empty { color: var(--muted); }
.ad-text { white-space: pre-wrap; }
.ad-mono { font-family: ui-monospace, monospace; font-size: .86rem; }
.ad-inline { display: inline; margin: 0; }
@media (max-width: 760px) {
  .ad-shell, html[data-nav="collapsed"] .ad-shell { grid-template-columns: minmax(0, 1fr); }
  .ad-sidebar { position: fixed; z-index: 20; top: 0; left: 0; width: 240px; height: 100vh; transform: translateX(-100%); transition: transform .18s ease; box-shadow: 0 0 24px rgba(0,0,0,.3); }
  body.ad-nav-open .ad-sidebar { transform: none; }
  html[data-nav="collapsed"] .ad-brand, html[data-nav="collapsed"] .ad-label { display: block; }
  html[data-nav="collapsed"] .ad-link { justify-content: flex-start; }
  .ad-content, .ad-head { padding-left: 16px; padding-right: 16px; }
}
@media (prefers-reduced-motion: reduce) { .ad-sidebar { transition: none; } }
.ad-sep { list-style: none; height: 1px; margin: 8px 6px; background: var(--line); }
.ad-content { transition: opacity .12s ease; }
.ad-content.is-leaving { opacity: 0; }
.ad-content.is-entering { animation: ad-in .16s ease; }
@keyframes ad-in { from { opacity: 0; } to { opacity: 1; } }
table[data-sortable] th[data-sort] { cursor: pointer; user-select: none; white-space: nowrap; }
table[data-sortable] th[aria-sort="ascending"]::after { content: " \25B2"; font-size: .7em; }
table[data-sortable] th[aria-sort="descending"]::after { content: " \25BC"; font-size: .7em; }
tr[data-detail] { cursor: pointer; }
tr[data-detail]:hover, tr[data-detail]:focus-visible { background: var(--control); outline: none; }
tr[data-detail].is-selected { background: var(--sea); }
tr.is-filtered-out { display: none; }
.ad-split { display: flex; gap: 0; min-height: 60vh; margin-top: 8px; }
.ad-split[data-orient="horizontal"] { flex-direction: row; }
.ad-split[data-orient="vertical"] { flex-direction: column; }
.ad-split > .ad-pane-list, .ad-split > .ad-pane-detail { min-width: 0; min-height: 0; overflow: auto; background: var(--panel); border: 1px solid var(--line); border-radius: 8px; padding: 10px 12px; }
.ad-split[data-orient="horizontal"] > .ad-pane-list { flex: 0 0 var(--split, 60%); }
.ad-split[data-orient="vertical"] > .ad-pane-list { flex: 0 0 var(--split, 55%); }
.ad-split[data-orient="horizontal"] > .ad-pane-detail { flex: 1 1 0; margin-left: 8px; }
.ad-split[data-orient="vertical"] > .ad-pane-detail { flex: 1 1 0; margin-top: 8px; }
.ad-gutter { flex: 0 0 8px; border-radius: 4px; background: var(--line); cursor: col-resize; touch-action: none; }
.ad-split[data-orient="vertical"] > .ad-gutter { cursor: row-resize; }
.ad-gutter:focus-visible { outline: 2px solid var(--accent); }
.ad-md p { margin: 4px 0; }
.ad-md h3, .ad-md h5 { margin: 8px 0 4px; font-size: 1rem; }
.ad-md pre { overflow-x: auto; background: var(--control); border: 1px solid var(--line); border-radius: 6px; padding: 8px; }
.ad-md code { font-family: ui-monospace, monospace; font-size: .88em; background: var(--control); border-radius: 4px; padding: 0 3px; }
.ad-md ul { margin: 4px 0; padding-left: 20px; }
.ad-note-item { border-top: 1px solid var(--line); padding: 8px 0; }
.ad-note-meta { color: var(--muted); font-size: .8rem; }
.ad-note-input { width: min(100%, 520px); min-height: 70px; }
.ad-dl { display: grid; grid-template-columns: max-content minmax(0, 1fr); gap: 4px 14px; margin: 8px 0 12px; }
.ad-dl dt { color: var(--muted); font-size: .86rem; }
.ad-dl dd { margin: 0; min-width: 0; overflow-wrap: anywhere; }
.ad-dl dt { color: var(--muted); font-size: .86rem; }
.ad-dl dd { margin: 0; min-width: 0; overflow-wrap: anywhere; }
.ad-split-tools { display: flex; gap: 8px; align-items: center; justify-content: flex-end; flex-wrap: wrap; }
.ad-split-tools input { font: inherit; color: var(--fg); background: var(--control); border: 1px solid var(--line); border-radius: 6px; padding: 4px 8px; }
@media (max-width: 760px) {
  .ad-split[data-orient="horizontal"] { flex-direction: column; }
  .ad-split[data-orient="horizontal"] > .ad-pane-list { flex: 0 0 var(--split, 55%); }
  .ad-split[data-orient="horizontal"] > .ad-pane-detail { margin-left: 0; margin-top: 8px; }
  .ad-split[data-orient="horizontal"] > .ad-gutter { flex: 0 0 8px; cursor: row-resize; }
}
@media (prefers-reduced-motion: reduce) { .ad-content, .ad-content.is-entering { transition: none; animation: none; } }
)ADC";
}

String AdminDashboardScript()
{
	return R"ADS((function () {
  var root = document.documentElement;
  var button = null;
  var key = 'ad-nav';
  var FADE_MS = 120;
  var leaving = [];
  var busy = null;

  // ---- sidebar collapse, remembered per browser ----
  try {
    if (window.localStorage && window.localStorage.getItem(key) === 'collapsed') {
      root.setAttribute('data-nav', 'collapsed');
    }
  } catch (e) { /* storage blocked: the sidebar starts expanded */ }
  function bindToggle() {
    if (!button) { return; }
    button.addEventListener('click', function () {
      var narrow = window.matchMedia && window.matchMedia('(max-width: 760px)').matches;
      if (narrow) {
        var open = document.body.classList.toggle('ad-nav-open');
        button.setAttribute('aria-expanded', open ? 'true' : 'false');
        return;
      }
      var collapsed = root.getAttribute('data-nav') === 'collapsed';
      if (collapsed) { root.removeAttribute('data-nav'); } else { root.setAttribute('data-nav', 'collapsed'); }
      button.setAttribute('aria-expanded', collapsed ? 'true' : 'false');
      try { window.localStorage.setItem(key, collapsed ? 'expanded' : 'collapsed'); } catch (e) { /* not persisted */ }
    });
  }

  // ---- in-place navigation: the page content is fetched and swapped, the shell stays ----
  function swapScripts(container) {
    var scripts = container.querySelectorAll('script');
    for (var i = 0; i < scripts.length; i++) {
      var fresh = document.createElement('script');
      fresh.text = scripts[i].textContent;
      scripts[i].parentNode.replaceChild(fresh, scripts[i]);
    }
  }
  function runLeaving() {
    var handlers = leaving;
    leaving = [];
    handlers.forEach(function (fn) { try { fn(); } catch (e) { /* a page's cleanup must not block the change */ } });
  }
  function navigate(href, push) {
    var content = document.querySelector('.ad-content');
    if (!window.fetch || !window.DOMParser || !content) { window.location.assign(href); return; }
    var token = {};
    busy = token;
    content.classList.add('is-leaving');
    var loaded = fetch(href, { credentials: 'same-origin', headers: { 'Accept': 'text/html' } }).then(function (res) {
      if (!res.ok) { throw new Error('status ' + res.status); }
      if ((res.headers.get('content-type') || '').indexOf('text/html') < 0) { throw new Error('not html'); }
      return res.text().then(function (text) { return { url: res.url || href, text: text }; });
    });
    var faded = new Promise(function (resolve) { setTimeout(resolve, FADE_MS); });
    Promise.all([loaded, faded]).then(function (parts) {
      if (busy !== token) { return; }
      var doc = new DOMParser().parseFromString(parts[0].text, 'text/html');
      var nextContent = doc.querySelector('.ad-content');
      var nextNav = doc.querySelector('.ad-sidebar');
      if (!nextContent || !nextNav) { throw new Error('no shell in the response'); }
      runLeaving();
      var nav = document.querySelector('.ad-sidebar');
      if (nav) { nav.innerHTML = nextNav.innerHTML; }
      var head = document.querySelector('.ad-head');
      var nextHead = doc.querySelector('.ad-head');
      if (head && nextHead) { head.innerHTML = nextHead.innerHTML; }
      else if (head) { head.parentNode.removeChild(head); }
      else if (nextHead) { var bar = document.querySelector('.ad-topbar'); if (bar) { bar.insertAdjacentElement('afterend', nextHead.cloneNode(true)); } }
      content.innerHTML = nextContent.innerHTML;
      swapScripts(content);
      document.title = doc.title;
      for (var i = 0; i < doc.body.attributes.length; i++) {
        var attr = doc.body.attributes[i];
        if (attr.name !== 'class') { document.body.setAttribute(attr.name, attr.value); }
      }
      if (push) { try { window.history.pushState({ adShell: true }, '', parts[0].url); } catch (e) { /* not recorded */ } }
      window.scrollTo(0, 0);
      content.classList.remove('is-leaving');
      content.classList.add('is-entering');
      setTimeout(function () { content.classList.remove('is-entering'); }, 200);
    }).catch(function () {
      if (busy === token) { window.location.assign(href); }
    });
  }
  document.addEventListener('click', function (ev) {
    if (ev.defaultPrevented || ev.button !== 0 || ev.metaKey || ev.ctrlKey || ev.shiftKey || ev.altKey) { return; }
    var a = ev.target && ev.target.closest ? ev.target.closest('a[href]') : null;
    if (!a || !a.closest('.ad-shell') || a.hasAttribute('download')) { return; }
    if ((a.target && a.target !== '_self') || a.getAttribute('data-nav') === 'full') { return; }
    var url;
    try { url = new URL(a.href, window.location.href); } catch (e) { return; }
    if (url.origin !== window.location.origin) { return; }
    if (url.pathname === window.location.pathname && url.search === window.location.search && url.hash) { return; }
    ev.preventDefault();
    navigate(url.href, true);
  });
  window.addEventListener('popstate', function (ev) {
    if (ev.state && ev.state.adShell) { navigate(window.location.href, false); }
  });
  try { window.history.replaceState({ adShell: true }, '', window.location.href); } catch (e) { /* not recorded */ }
  // Defined before any page script runs, so pages can register leave handlers as they load.
  window.AdShell = {
    navigate: function (href) { navigate(href, true); },
    reload: function () { navigate(window.location.href, false); },
    onLeave: function (fn) { leaving.push(fn); }
  };

  // ---- sortable tables: th[data-sort] inside table[data-sortable]; cells carry data-v for the sort value ----
  function rowsOf(table) {
    return Array.prototype.filter.call(table.querySelectorAll('tr'), function (tr) { return !tr.querySelector('th'); });
  }
  function sortTable(th) {
    var table = th.closest('table');
    if (!table) { return; }
    var idx = Array.prototype.indexOf.call(th.parentNode.children, th);
    var current = th.getAttribute('aria-sort');
    var dir = current === 'ascending' ? 'descending' : current === 'descending' ? 'ascending'
            : (th.getAttribute('data-sort-default') || 'ascending');
    var numeric = th.getAttribute('data-sort') === 'num';
    var rows = rowsOf(table);
    if (!rows.length) { return; }
    var keyed = rows.map(function (tr, i) {
      var cell = tr.children[idx];
      var raw = cell ? (cell.getAttribute('data-v') || cell.textContent.trim()) : '';
      return { tr: tr, i: i, v: numeric ? (parseFloat(raw) || 0) : raw.toLowerCase() };
    });
    keyed.sort(function (a, b) {
      var c = a.v < b.v ? -1 : a.v > b.v ? 1 : 0;
      if (dir === 'descending') { c = -c; }
      return c || a.i - b.i;
    });
    var parent = rows[0].parentNode;
    keyed.forEach(function (k) { parent.appendChild(k.tr); });
    Array.prototype.forEach.call(th.parentNode.children, function (other) { other.removeAttribute('aria-sort'); });
    th.setAttribute('aria-sort', dir);
  }
  document.addEventListener('click', function (ev) {
    var th = ev.target && ev.target.closest ? ev.target.closest('table[data-sortable] th[data-sort]') : null;
    if (th) { sortTable(th); }
  });

  // ---- filter boxes: input[data-filter-for="tableId"] hides rows whose text does not match ----
  document.addEventListener('input', function (ev) {
    var box = ev.target;
    if (!box || !box.getAttribute || !box.getAttribute('data-filter-for')) { return; }
    var table = document.getElementById(box.getAttribute('data-filter-for'));
    if (!table) { return; }
    var q = box.value.trim().toLowerCase();
    rowsOf(table).forEach(function (tr) {
      tr.classList.toggle('is-filtered-out', q.length > 0 && tr.textContent.toLowerCase().indexOf(q) < 0);
    });
  });

  // ---- selectable rows: tr[data-detail="id"] shows <template id="id"> in the detail pane ----
  function select(tr) {
    var split = tr.closest('.ad-split');
    var pane = split ? split.querySelector('.ad-pane-detail') : null;
    var tpl = document.getElementById(tr.getAttribute('data-detail'));
    if (!pane || !tpl || !tpl.content) { return; }
    Array.prototype.forEach.call(tr.closest('table').querySelectorAll('tr.is-selected'), function (other) {
      other.classList.remove('is-selected');
      other.removeAttribute('aria-selected');
    });
    tr.classList.add('is-selected');
    tr.setAttribute('aria-selected', 'true');
    while (pane.firstChild) { pane.removeChild(pane.firstChild); }
    pane.appendChild(document.importNode(tpl.content, true));
    swapScripts(pane);
  }
  document.addEventListener('click', function (ev) {
    var tr = ev.target && ev.target.closest ? ev.target.closest('tr[data-detail]') : null;
    if (tr && !(ev.target.closest && ev.target.closest('a, button, input, select, textarea, form'))) { select(tr); }
  });
  document.addEventListener('keydown', function (ev) {
    if (ev.key !== 'Enter' || !ev.target || !ev.target.getAttribute) { return; }
    if (ev.target.hasAttribute('data-detail')) { select(ev.target); }
  });

  // ---- split panes: the gutter resizes the list pane; orientation and size are remembered per split id ----
  function splitKey(split) { return 'ad-split:' + (split.getAttribute('data-split') || ''); }
  function readSplit(split) {
    try { return JSON.parse(window.localStorage.getItem(splitKey(split)) || '{}') || {}; } catch (e) { return {}; }
  }
  function writeSplit(split, state) {
    try { window.localStorage.setItem(splitKey(split), JSON.stringify(state)); } catch (e) { /* not remembered */ }
  }
  function applySplit(split, state) {
    if (state.orient === 'vertical' || state.orient === 'horizontal') { split.setAttribute('data-orient', state.orient); }
    if (typeof state.split === 'number') { split.style.setProperty('--split', state.split + '%'); }
  }
  function restoreSplits() {
    Array.prototype.forEach.call(document.querySelectorAll('.ad-split[data-split]'), function (split) {
      applySplit(split, readSplit(split));
    });
  }
  document.addEventListener('click', function (ev) {
    var btn = ev.target && ev.target.closest ? ev.target.closest('[data-split-toggle]') : null;
    if (!btn) { return; }
    var split = btn.closest('.ad-split') || document.querySelector('.ad-split[data-split="' + btn.getAttribute('data-split-toggle') + '"]');
    if (!split) { return; }
    var state = readSplit(split);
    state.orient = split.getAttribute('data-orient') === 'vertical' ? 'horizontal' : 'vertical';
    applySplit(split, state);
    writeSplit(split, state);
  });
  var drag = null;
  document.addEventListener('pointerdown', function (ev) {
    var gutter = ev.target && ev.target.closest ? ev.target.closest('.ad-gutter') : null;
    if (!gutter) { return; }
    drag = { split: gutter.closest('.ad-split'), gutter: gutter };
    gutter.setPointerCapture && gutter.setPointerCapture(ev.pointerId);
    ev.preventDefault();
  });
  document.addEventListener('pointermove', function (ev) {
    if (!drag || !drag.split) { return; }
    var rect = drag.split.getBoundingClientRect();
    var vertical = drag.split.getAttribute('data-orient') === 'vertical';
    var pct = vertical ? (ev.clientY - rect.top) / rect.height * 100 : (ev.clientX - rect.left) / rect.width * 100;
    pct = Math.max(15, Math.min(85, Math.round(pct)));
    drag.split.style.setProperty('--split', pct + '%');
    drag.pct = pct;
  });
  document.addEventListener('pointerup', function () {
    if (!drag || !drag.split) { return; }
    if (typeof drag.pct === 'number') {
      var state = readSplit(drag.split);
      state.split = drag.pct;
      writeSplit(drag.split, state);
    }
    drag = null;
  });
  document.addEventListener('keydown', function (ev) {
    if (!ev.target || !ev.target.classList || !ev.target.classList.contains('ad-gutter')) { return; }
    var split = ev.target.closest('.ad-split');
    var vertical = split.getAttribute('data-orient') === 'vertical';
    var step = 0;
    if ((vertical && ev.key === 'ArrowUp') || (!vertical && ev.key === 'ArrowLeft')) { step = -5; }
    if ((vertical && ev.key === 'ArrowDown') || (!vertical && ev.key === 'ArrowRight')) { step = 5; }
    if (!step) { return; }
    ev.preventDefault();
    var current = parseFloat(split.style.getPropertyValue('--split')) || 55;
    var next = Math.max(15, Math.min(85, current + step));
    split.style.setProperty('--split', next + '%');
    var state = readSplit(split);
    state.split = next;
    writeSplit(split, state);
  });

  function ready() {
    button = document.getElementById('ad-toggle');
    bindToggle();
    restoreSplits();
  }
  if (document.readyState === 'loading') { document.addEventListener('DOMContentLoaded', ready); } else { ready(); }
})();)ADS";
}

String RenderAdminShell(const WebsiteShell& s)
{
	StringBuffer b;
	b << "<!doctype html><html" << s.html_attributes << "><head><meta charset=\"utf-8\">"
	  << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
	  << s.head_script << "<title>" << ShellEsc(s.title) << "</title>"
	  << "<style>" << WebsiteThemeStyles() << WebsiteDensityStyles() << "</style>"
	  << "<style>" << AdminDashboardCss() << "</style>"
	  << "<script>" << AdminDashboardScript() << "</script></head>"
	  << "<body class=\"ad-body\"" << s.body_attributes << "><div class=\"ad-shell\">"
	  << "<aside class=\"ad-sidebar\" id=\"ad-sidebar\">"
	  << "<a class=\"ad-brand\" href=\"" << ShellEsc(s.brand_href) << "\">" << ShellEsc(s.brand) << "</a>"
	  << "<nav aria-label=\"Main\"><ul>";
	for(const WebsiteShellLink& l : s.nav) {
		if(l.separator_before)
			b << "<li class=\"ad-sep\" role=\"separator\"></li>";
		b << "<li><a class=\"ad-link" << (l.active ? " is-active" : String()) << "\" href=\"" << ShellEsc(l.href)
		  << "\"" << (l.active ? " aria-current=\"page\"" : String()) << ">" << ShellIcon(l.icon)
		  << "<span class=\"ad-label\">" << ShellEsc(l.label) << "</span></a></li>";
	}
	b << "</ul></nav></aside>"
	  << "<div class=\"ad-main\"><header class=\"ad-topbar\">"
	  << "<button type=\"button\" class=\"ad-toggle\" id=\"ad-toggle\" aria-controls=\"ad-sidebar\" aria-expanded=\"true\""
	  << " aria-label=\"Toggle sidebar\">" << ShellIcon("list") << "</button>"
	  << "<div class=\"ad-topbar-end\">" << s.topbar_html << "</div></header>";
	if(s.crumbs.GetCount()) {
		b << "<div class=\"ad-head\"><nav class=\"ad-crumbs\" aria-label=\"Breadcrumb\"><ol>";
		for(int i = 0; i < s.crumbs.GetCount(); i++) {
			const WebsiteShellLink& c = s.crumbs[i];
			if(i == s.crumbs.GetCount() - 1)
				b << "<li aria-current=\"page\">" << ShellEsc(c.label) << "</li>";
			else
				b << "<li><a href=\"" << ShellEsc(c.href) << "\">" << ShellEsc(c.label) << "</a></li>";
		}
		b << "</ol></nav></div>";
	}
	b << "<main class=\"ad-content\">" << s.body << "</main></div></div>"
	  << WebsiteThemeControlsScript() << "</body></html>";
	return String(b);
}

END_UPP_NAMESPACE
