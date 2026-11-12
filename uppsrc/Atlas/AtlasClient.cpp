#include "AtlasSite.h"

NAMESPACE_UPP

// The page's look and its one client module. Both are inline: the portal
// loads no third-party script or stylesheet (the 028 lesson). The module draws
// the map from the JSON the server embeds and never builds markup from text:
// names reach the DOM through textContent.

String AtlasStylesheet()
{
	return R"ATLAS(<style>
:root { --bg: #f7f7f5; --fg: #1b1b1b; --muted: #5c5c5c; --line: #d0d0cc; --panel: #ffffff; --link: #1864ab; }
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) { --bg: #151515; --fg: #ececec; --muted: #a0a0a0; --line: #3a3a3a; --panel: #1f1f1f; --link: #74c0fc; }
}
:root[data-theme="dark"] { --bg: #151515; --fg: #ececec; --muted: #a0a0a0; --line: #3a3a3a; --panel: #1f1f1f; --link: #74c0fc; }
body { background: var(--bg); color: var(--fg); font: 15px/1.45 system-ui, sans-serif; margin: 0; }
main { padding: 16px; max-width: 1100px; margin: 0 auto; }
a { color: var(--link); }
nav.atlas-nav { padding: 10px 16px; border-bottom: 1px solid var(--line); }
nav.atlas-nav a { font-weight: 600; text-decoration: none; }
h1 { font-size: 1.5rem; margin: 8px 0; }
h2 { font-size: 1.15rem; margin-top: 24px; }
table { border-collapse: collapse; width: 100%; margin: 8px 0; display: block; overflow-x: auto; }
th, td { border-bottom: 1px solid var(--line); padding: 6px 8px; text-align: left; vertical-align: top; }
.meta, .hint { color: var(--muted); font-size: 0.9rem; }
.err { color: #c92a2a; font-weight: 600; }
.toolbar { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; margin: 8px 0; }
#atlas-plane { font-weight: 600; }
#atlas-canvas { width: 100%; height: 70vh; min-height: 360px; background: var(--panel); border: 1px solid var(--line); border-radius: 8px; touch-action: none; cursor: grab; }
#atlas-canvas .plane { fill: var(--panel); }
#atlas-canvas .sea { fill: #d0ebff; opacity: 0.7; }
#atlas-canvas .el { stroke: var(--line); stroke-width: 1.5; fill: var(--panel); }
#atlas-canvas .el.kind-block { stroke-width: 2.5; }
#atlas-canvas .el.kind-continent { stroke-width: 3; }
#atlas-canvas .el.status-hot { stroke: #e8590c; fill: #fff4e6; }
#atlas-canvas .el.status-active { stroke: #2b8a3e; }
#atlas-canvas .el.status-planned { stroke: #1c7ed6; }
#atlas-canvas .el.status-not-ready { stroke: #868e96; stroke-dasharray: 6 4; }
#atlas-canvas .el.status-future { stroke: #9775fa; stroke-dasharray: 2 4; }
#atlas-canvas .el.status-settled { stroke: #495057; fill: #e9ecef; }
#atlas-canvas .el.status-trouble { stroke: #c92a2a; fill: #ffe3e3; }
#atlas-canvas .label { fill: var(--fg); font: 600 14px system-ui, sans-serif; pointer-events: none; }
#atlas-canvas .badge { fill: var(--muted); font: 12px system-ui, sans-serif; pointer-events: none; }
#atlas-canvas .road { fill: none; stroke: var(--muted); stroke-width: 2; }
#atlas-canvas .road.kind-communication { stroke-dasharray: 6 5; }
#atlas-canvas .road.kind-dependency { stroke-dasharray: 2 4; stroke: #c92a2a; }
#atlas-canvas .road-label { fill: var(--muted); font: 12px system-ui, sans-serif; }
.status { display: inline-block; padding: 1px 8px; border-radius: 999px; border: 1px solid var(--line); font-size: 0.85rem; }
.status-hot { background: #fff4e6; border-color: #e8590c; }
.status-trouble { background: #ffe3e3; border-color: #c92a2a; }
details.edit, details { border: 1px solid var(--line); border-radius: 8px; padding: 8px 12px; margin: 16px 0; background: var(--panel); }
details summary { cursor: pointer; font-weight: 600; }
form { display: flex; flex-wrap: wrap; gap: 8px 12px; align-items: flex-end; margin: 12px 0; }
form h3 { width: 100%; font-size: 1rem; margin: 8px 0 0; }
label { display: flex; flex-direction: column; font-size: 0.9rem; gap: 2px; }
label:has(input[type="checkbox"]) { flex-direction: row; align-items: center; }
input, select, textarea, button { font: inherit; color: var(--fg); background: var(--bg); border: 1px solid var(--line); border-radius: 6px; padding: 4px 8px; }
textarea { width: 100%; font-family: ui-monospace, monospace; }
button { cursor: pointer; font-weight: 600; }
dl.facts { display: grid; grid-template-columns: max-content 1fr; gap: 6px 16px; }
dl.facts dt { color: var(--muted); }
dl.facts dd { margin: 0; }
ol.path { padding-left: 20px; }
@media (max-width: 600px) { main { padding: 12px; } #atlas-canvas { min-height: 320px; } }
</style>)ATLAS";
}

String AtlasScript()
{
	return R"ATLAS(
(function () {
  'use strict';
  var body = document.body;
  var editable = body.getAttribute('data-editable') === '1';
  var csrf = body.getAttribute('data-csrf') || '';
  var base = body.getAttribute('data-base') || '';
  var svg = document.getElementById('atlas-canvas');
  var planeLabel = document.getElementById('atlas-plane');
  var upButton = document.getElementById('atlas-up');
  var NS = 'http://www.w3.org/2000/svg';
  var LEVEL_BADGE_MIN_SCALE = 0.5;
  var data = JSON.parse(document.getElementById('atlas-data').textContent);
  var map = data.map;
  var revision = data.revision;
  var view = { plane: '', scale: 1, tx: 24, ty: 24 };
  var drag = null;

  function find(id) {
    for (var i = 0; i < map.elements.length; i++) {
      if (map.elements[i].id === id) { return map.elements[i]; }
    }
    return null;
  }

  function node(name, attrs, text) {
    var n = document.createElementNS(NS, name);
    for (var k in attrs) { n.setAttribute(k, attrs[k]); }
    if (text !== undefined) { n.textContent = text; }
    return n;
  }

  function childCount(id) {
    return map.elements.filter(function (e) { return e.parent === id; }).length;
  }

  function planeNames(plane) {
    var names = [];
    var cur = plane;
    while (cur) {
      var e = find(cur);
      if (!e) { break; }
      names.unshift(e.name);
      cur = e.parent;
    }
    return names;
  }

  function readHash() {
    var m = /plane=([A-Za-z0-9_-]*)/.exec(window.location.hash);
    view.plane = m && find(m[1]) ? m[1] : '';
  }

  function writeHash() {
    var h = view.plane ? '#plane=' + view.plane : '';
    if (window.history && window.history.replaceState) {
      window.history.replaceState(null, '', window.location.pathname + window.location.search + h);
    }
  }

  function enter(plane) {
    view.plane = plane;
    view.scale = 1;
    view.tx = 24;
    view.ty = 24;
    writeHash();
    draw();
  }

  function draw() {
    while (svg.firstChild) { svg.removeChild(svg.firstChild); }
    var W = svg.clientWidth || 900;
    var H = svg.clientHeight || 600;
    svg.setAttribute('viewBox', '0 0 ' + W + ' ' + H);
    var names = planeNames(view.plane);
    planeLabel.textContent = names.length ? names.join(' / ') : 'World plane';
    if (upButton) { upButton.disabled = !view.plane; }

    var root = node('g', { transform: 'translate(' + view.tx + ',' + view.ty + ') scale(' + view.scale + ')' });
    svg.appendChild(root);
    var parent = view.plane ? find(view.plane) : null;
    var pw = parent ? parent.w : map.world.width;
    var ph = parent ? parent.h : map.world.height;
    root.appendChild(node('rect', { x: 0, y: 0, width: pw, height: ph, 'class': 'plane' }));

    var here = map.elements.filter(function (e) { return e.parent === view.plane; });
    here.forEach(function (e) {
      if (e.kind === 'sea') {
        root.appendChild(node('rect', { x: e.x, y: e.y, width: e.w, height: e.h, rx: 24, 'class': 'sea' }));
      }
    });

    map.roads.forEach(function (r) {
      var a = find(r.from);
      var b = find(r.to);
      if (!a || !b || a.parent !== view.plane || b.parent !== view.plane) { return; }
      var x1 = a.x + a.w / 2, y1 = a.y + a.h / 2, x2 = b.x + b.w / 2, y2 = b.y + b.h / 2;
      root.appendChild(node('path', {
        d: 'M' + x1 + ' ' + y1 + ' L' + x2 + ' ' + y2,
        'class': 'road kind-' + r.kind
      }));
      if (r.label) {
        root.appendChild(node('text', { x: (x1 + x2) / 2, y: (y1 + y2) / 2 - 4, 'class': 'road-label' }, r.label));
      }
    });

    here.forEach(function (e) {
      if (e.kind === 'sea') { return; }
      var g = node('g', { 'data-id': e.id });
      g.appendChild(node('rect', {
        x: e.x, y: e.y, width: e.w, height: e.h, rx: 12,
        'class': 'el kind-' + e.kind + ' status-' + e.status
      }));
      g.appendChild(node('text', { x: e.x + 12, y: e.y + 24, 'class': 'label' }, e.name));
      var inside = childCount(e.id);
      if (inside && view.scale >= LEVEL_BADGE_MIN_SCALE) {
        g.appendChild(node('text', { x: e.x + 12, y: e.y + e.h - 12, 'class': 'badge' }, inside + ' inside'));
      }
      root.appendChild(g);
    });
    here.forEach(function (e) {
      if (e.kind !== 'sea') { return; }
      root.appendChild(node('text', { x: e.x + 12, y: e.y + 24, 'class': 'label' }, e.name));
    });
  }

  function zoomAt(px, py, factor) {
    var next = Math.min(4, Math.max(0.2, view.scale * factor));
    var k = next / view.scale;
    view.tx = px - (px - view.tx) * k;
    view.ty = py - (py - view.ty) * k;
    view.scale = next;
    draw();
  }

  function saveMove(e) {
    var form = 'id=' + encodeURIComponent(e.id) + '&x=' + e.x + '&y=' + e.y
             + '&expected_revision=' + encodeURIComponent(revision);
    fetch(base + '/element/move', {
      method: 'POST',
      credentials: 'same-origin',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded', 'x-wf-csrf': csrf },
      body: form
    }).then(function (res) {
      return res.json().then(function (j) { return { status: res.status, body: j }; });
    }).then(function (res) {
      if (res.status === 200 && res.body.ok) {
        revision = res.body.revision;
      } else {
        window.alert('The move was not saved (' + (res.body.error || res.status) + '). The page will reload.');
        window.location.reload();
      }
    }).catch(function () {
      window.location.reload();
    });
  }

  svg.addEventListener('pointerdown', function (ev) {
    var hit = ev.target && ev.target.closest ? ev.target.closest('[data-id]') : null;
    var id = hit ? hit.getAttribute('data-id') : '';
    var e = id ? find(id) : null;
    drag = {
      id: e ? e.id : '', startX: ev.clientX, startY: ev.clientY,
      tx0: view.tx, ty0: view.ty, origX: e ? e.x : 0, origY: e ? e.y : 0, moved: false
    };
    svg.setPointerCapture(ev.pointerId);
  });

  svg.addEventListener('pointermove', function (ev) {
    if (!drag) { return; }
    var dx = ev.clientX - drag.startX;
    var dy = ev.clientY - drag.startY;
    if (!drag.moved && Math.abs(dx) + Math.abs(dy) < 4) { return; }
    drag.moved = true;
    if (drag.id) {
      if (!editable) { return; }
      var e = find(drag.id);
      e.x = Math.max(0, Math.round(drag.origX + dx / view.scale));
      e.y = Math.max(0, Math.round(drag.origY + dy / view.scale));
      draw();
    } else {
      view.tx = drag.tx0 + dx;
      view.ty = drag.ty0 + dy;
      draw();
    }
  });

  svg.addEventListener('pointerup', function () {
    var d = drag;
    drag = null;
    if (!d) { return; }
    if (!d.id) { return; }
    var e = find(d.id);
    if (d.moved) {
      if (editable && (e.x !== d.origX || e.y !== d.origY)) { saveMove(e); }
      return;
    }
    if (childCount(e.id)) {
      enter(e.id);
    } else if (base) {
      window.location.href = base + '/element/' + encodeURIComponent(e.id);
    }
  });

  svg.addEventListener('wheel', function (ev) {
    ev.preventDefault();
    var rect = svg.getBoundingClientRect();
    zoomAt(ev.clientX - rect.left, ev.clientY - rect.top, ev.deltaY < 0 ? 1.1 : 1 / 1.1);
  }, { passive: false });

  if (upButton) {
    upButton.addEventListener('click', function () {
      var cur = view.plane ? find(view.plane) : null;
      enter(cur ? cur.parent : '');
    });
  }

  window.addEventListener('hashchange', function () { readHash(); draw(); });
  window.addEventListener('resize', draw);
  readHash();
  draw();
})();
)ATLAS";
}

END_UPP_NAMESPACE
