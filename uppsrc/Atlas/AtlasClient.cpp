#include "AtlasSite.h"

NAMESPACE_UPP

// The page's look and its one client module. Both are inline: the portal
// loads no third-party script or stylesheet (the 028 lesson). The module draws
// the map from the JSON the server embeds and never builds markup from text:
// names reach the DOM through textContent.

String AtlasStylesheet()
{
	return R"ATLAS(<style>
:root { --link: var(--accent); }
body { background: var(--bg); color: var(--fg); font: var(--fs)/1.45 system-ui, sans-serif; margin: 0; }
main { padding: calc(var(--pad) * 2); max-width: var(--wf-container-width); margin: 0 auto; }
a { color: var(--link); }
nav.atlas-nav { padding: var(--pad) calc(var(--pad) * 2); border-bottom: 1px solid var(--line); display: flex; flex-wrap: wrap; gap: var(--gap); align-items: center; }
nav.atlas-nav a { font-weight: 600; text-decoration: none; }
h1 { font-size: 1.5rem; margin: 8px 0; }
h2 { font-size: 1.15rem; margin-top: calc(var(--gap) * 1.5); }
table { border-collapse: collapse; width: 100%; margin: 8px 0; display: block; overflow-x: auto; }
th, td { border-bottom: 1px solid var(--line); padding: var(--pad) calc(var(--pad) * 2); text-align: left; vertical-align: top; }
.meta, .hint { color: var(--muted); font-size: 0.9rem; }
.err { color: var(--st-trouble); font-weight: 600; }
.toolbar { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; margin: 8px 0; }
#atlas-plane { font-weight: 600; }
#atlas-canvas { width: 100%; height: 70vh; min-height: 360px; background: var(--panel); border: 1px solid var(--line); border-radius: 8px; touch-action: none; cursor: grab; }
#atlas-canvas .plane { fill: var(--panel); }
#atlas-stage.atlas-max { position: fixed; inset: 0; z-index: 1000; background: var(--bg); padding: var(--pad); display: flex; flex-direction: column; }
#atlas-stage.atlas-max #atlas-canvas { flex: 1 1 auto; height: auto; min-height: 0; border-radius: 0; }
#atlas-stage.atlas-max .toolbar { margin: 0 0 var(--pad); }
body.atlas-lock { overflow: hidden; }
#atlas-canvas g[data-id].hover > rect.el, #atlas-canvas g[data-id].hover > rect.sea { stroke: var(--accent); stroke-width: 3; }
#atlas-canvas path.road.lit { stroke: var(--accent); stroke-width: 3; }
#atlas-zoom { min-width: 3.5em; text-align: right; font-variant-numeric: tabular-nums; }
#atlas-close[hidden], #atlas-maximize[hidden] { display: none; }
#atlas-canvas .sea { fill: var(--sea); opacity: 0.7; }
#atlas-canvas .el { stroke: var(--line); stroke-width: 1.5; fill: var(--panel); }
#atlas-canvas .el.kind-block { stroke-width: 2.5; }
#atlas-canvas .el.kind-continent { stroke-width: 3; }
#atlas-canvas .el.status-hot { stroke: var(--st-hot); fill: color-mix(in srgb, var(--st-hot) 18%, var(--panel)); }
#atlas-canvas .el.status-active { stroke: var(--st-active); }
#atlas-canvas .el.status-planned { stroke: var(--st-planned); }
#atlas-canvas .el.status-not-ready { stroke: var(--st-notready); stroke-dasharray: 6 4; }
#atlas-canvas .el.status-future { stroke: var(--st-future); stroke-dasharray: 2 4; }
#atlas-canvas .el.status-settled { stroke: var(--st-settled); fill: color-mix(in srgb, var(--st-settled) 12%, var(--panel)); }
#atlas-canvas .el.status-trouble { stroke: var(--st-trouble); fill: color-mix(in srgb, var(--st-trouble) 16%, var(--panel)); }
#atlas-canvas .label { fill: var(--fg); font: 600 calc(var(--fs) - 1px) system-ui, sans-serif; pointer-events: none; }
#atlas-canvas .badge { fill: var(--muted); font: 12px system-ui, sans-serif; pointer-events: none; }
#atlas-canvas .road { fill: none; stroke: var(--muted); stroke-width: 2; }
#atlas-canvas .road.kind-communication { stroke-dasharray: 6 5; }
#atlas-canvas .road.kind-dependency { stroke-dasharray: 2 4; stroke: var(--st-trouble); }
#atlas-canvas .road-label { fill: var(--muted); font: 12px system-ui, sans-serif; }
.status { display: inline-block; padding: 1px 8px; border-radius: 999px; border: 1px solid var(--line); font-size: 0.85rem; }
.status-hot { background: color-mix(in srgb, var(--st-hot) 18%, var(--panel)); border-color: var(--st-hot); }
.status-trouble { background: color-mix(in srgb, var(--st-trouble) 16%, var(--panel)); border-color: var(--st-trouble); }
details.edit, details { border: 1px solid var(--line); border-radius: 8px; padding: var(--pad) calc(var(--pad) * 2); margin: var(--gap) 0; background: var(--panel); }
details summary { cursor: pointer; font-weight: 600; }
form { display: flex; flex-wrap: wrap; gap: var(--pad) calc(var(--pad) * 2); align-items: flex-end; margin: var(--gap) 0; }
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
@media (max-width: 600px) { main { padding: var(--pad); } #atlas-canvas { min-height: 320px; } }
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
  var stage = document.getElementById('atlas-stage');
  var svg = document.getElementById('atlas-canvas');
  var planeLabel = document.getElementById('atlas-plane');
  var upButton = document.getElementById('atlas-up');
  var contentsBox = document.getElementById('atlas-contents');
  var zoomLabel = document.getElementById('atlas-zoom');
  var maximizeButton = document.getElementById('atlas-maximize');
  var closeButton = document.getElementById('atlas-close');
  var NS = 'http://www.w3.org/2000/svg';
  // Overview tiles: one size for every block, so titles stay readable whatever the block sizes stored.
  var TILE_W = 220, TILE_H = 96, TILE_GAP = 24, TILE_PAD = 12;
  // The floor is low enough that Fit can show a layout many times the stored world size.
  var MIN_SCALE = 0.02, MAX_SCALE = 6, FIT_MAX_SCALE = 2;
  var data = JSON.parse(document.getElementById('atlas-data').textContent);
  var map = data.map;
  var revision = data.revision;
  // manual: the user zoomed or panned, so draws keep the view; otherwise the view fits the content.
  var view = { plane: '', scale: 1, tx: 24, ty: 24, manual: false, root: null, tiles: {},
               contentW: 1, contentH: 1, hover: '' };
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

  function childrenOf(id) {
    return map.elements.filter(function (e) { return e.parent === id; });
  }

  function childCount(id) {
    return childrenOf(id).length;
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

  // The ancestor-or-self of an element that sits directly on the current plane.
  function inPlane(id) {
    var cur = find(id);
    while (cur && cur.parent !== view.plane) { cur = cur.parent ? find(cur.parent) : null; }
    return cur;
  }

  // Position of an element relative to the current plane, summing the stored offsets of its parents.
  function absPos(e) {
    var x = e.x, y = e.y, p = e.parent ? find(e.parent) : null;
    while (p && p.id !== view.plane) {
      x += p.x;
      y += p.y;
      p = p.parent ? find(p.parent) : null;
    }
    return { x: x, y: y, w: e.w, h: e.h };
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
    view.manual = false;
    writeHash();
    draw();
  }

  function applyTransform() {
    if (view.root) {
      view.root.setAttribute('transform', 'translate(' + view.tx + ',' + view.ty + ') scale(' + view.scale + ')');
    }
    if (zoomLabel) { zoomLabel.textContent = Math.round(view.scale * 100) + '%'; }
  }

  function fitView() {
    var W = svg.clientWidth || 900;
    var H = svg.clientHeight || 600;
    var s = Math.min((W - 40) / view.contentW, (H - 40) / view.contentH, FIT_MAX_SCALE);
    view.scale = Math.max(MIN_SCALE, s);
    view.tx = (W - view.contentW * view.scale) / 2;
    view.ty = (H - view.contentH * view.scale) / 2;
    applyTransform();
  }

  function actualSize() {
    var W = svg.clientWidth || 900;
    var H = svg.clientHeight || 600;
    view.manual = true;
    view.scale = 1;
    view.tx = (W - view.contentW) / 2;
    view.ty = (H - view.contentH) / 2;
    applyTransform();
  }

  function zoomAt(px, py, factor) {
    var next = Math.min(MAX_SCALE, Math.max(MIN_SCALE, view.scale * factor));
    var k = next / view.scale;
    view.manual = true;
    view.tx = px - (px - view.tx) * k;
    view.ty = py - (py - view.ty) * k;
    view.scale = next;
    applyTransform();
  }

  function zoomCentre(factor) {
    zoomAt((svg.clientWidth || 900) / 2, (svg.clientHeight || 600) / 2, factor);
  }

  // Overview: a near-square grid of equal tiles, in the order the stored layout gives them.
  function layoutTiles(kids) {
    view.tiles = {};
    var n = kids.length;
    if (!n) { return { w: 0, h: 0 }; }
    var W = svg.clientWidth || 900;
    var H = svg.clientHeight || 600;
    var sorted = kids.slice().sort(function (a, b) { return a.y - b.y || a.x - b.x; });
    var cols = Math.min(n, Math.max(1, Math.round(Math.sqrt(n * (W / H)))));
    var rows = Math.ceil(n / cols);
    sorted.forEach(function (e, i) {
      view.tiles[e.id] = {
        x: (i % cols) * (TILE_W + TILE_GAP),
        y: Math.floor(i / cols) * (TILE_H + TILE_GAP),
        w: TILE_W, h: TILE_H
      };
    });
    return { w: cols * TILE_W + (cols - 1) * TILE_GAP, h: rows * TILE_H + (rows - 1) * TILE_GAP };
  }

  function tileFont(name) {
    return Math.max(12, Math.min(22, (TILE_W - 2 * TILE_PAD) / Math.max(6, name.length * 0.56)));
  }

  // Contents: title size follows the block height, so nested blocks stay readable.
  function titleFont(e) {
    return Math.max(11, Math.min(28, e.h * 0.16));
  }

  function drawTree(parentG, e) {
    var g = node('g', { 'data-id': e.id, transform: 'translate(' + e.x + ',' + e.y + ')' });
    if (e.kind === 'sea') {
      g.appendChild(node('rect', { x: 0, y: 0, width: e.w, height: e.h, rx: 24, 'class': 'sea' }));
    } else {
      g.appendChild(node('rect', {
        x: 0, y: 0, width: e.w, height: e.h, rx: 12,
        'class': 'el kind-' + e.kind + ' status-' + e.status
      }));
    }
    var fs = titleFont(e);
    g.appendChild(node('text', { x: 12, y: fs + 6, 'class': 'label', style: 'font-size:' + fs + 'px' }, e.name));
    var inside = childCount(e.id);
    if (inside && e.h >= 60) {
      g.appendChild(node('text', { x: 12, y: e.h - 10, 'class': 'badge' }, inside + ' inside'));
    }
    childrenOf(e.id).forEach(function (c) { drawTree(g, c); });
    parentG.appendChild(g);
  }

  // Road end on the drawn map: the block itself in contents mode, the tile holding it in overview.
  function endpoint(id, contents) {
    var e = find(id);
    if (!e) { return null; }
    if (contents) {
      if (!inPlane(id)) { return null; }
      var p = absPos(e);
      return { id: e.id, cx: p.x + p.w / 2, cy: p.y + p.h / 2 };
    }
    var top = inPlane(id);
    var t = top ? view.tiles[top.id] : null;
    return t ? { id: top.id, cx: t.x + t.w / 2, cy: t.y + t.h / 2 } : null;
  }

  function drawRoads(root, contents) {
    var seen = {};
    map.roads.forEach(function (r) {
      var a = endpoint(r.from, contents);
      var b = endpoint(r.to, contents);
      if (!a || !b || a.id === b.id) { return; }
      var key = a.id + '>' + b.id;
      if (seen[key]) { return; }
      seen[key] = true;
      root.appendChild(node('path', {
        d: 'M' + a.cx + ' ' + a.cy + ' L' + b.cx + ' ' + b.cy,
        'class': 'road kind-' + r.kind,
        'data-a': a.id, 'data-b': b.id
      }));
      if (r.label) {
        root.appendChild(node('text', { x: (a.cx + b.cx) / 2, y: (a.cy + b.cy) / 2 - 4, 'class': 'road-label' }, r.label));
      }
    });
  }

  function markHover() {
    var els = svg.querySelectorAll('[data-id]');
    for (var i = 0; i < els.length; i++) {
      els[i].classList.toggle('hover', !!view.hover && els[i].getAttribute('data-id') === view.hover);
    }
    var roads = svg.querySelectorAll('path.road');
    for (var j = 0; j < roads.length; j++) {
      var a = roads[j].getAttribute('data-a');
      var b = roads[j].getAttribute('data-b');
      roads[j].classList.toggle('lit', !!view.hover && (a === view.hover || b === view.hover));
    }
  }

  function setHover(id) {
    if (id === view.hover) { return; }
    view.hover = id;
    markHover();
  }

  function draw() {
    while (svg.firstChild) { svg.removeChild(svg.firstChild); }
    var W = svg.clientWidth || 900;
    var H = svg.clientHeight || 600;
    svg.setAttribute('viewBox', '0 0 ' + W + ' ' + H);
    var names = planeNames(view.plane);
    planeLabel.textContent = names.length ? names.join(' / ') : 'World plane';
    if (upButton) { upButton.disabled = !view.plane; }

    var contents = !!(contentsBox && contentsBox.checked);
    var root = node('g', {});
    svg.appendChild(root);
    view.root = root;
    var kids = childrenOf(view.plane);
    view.tiles = {};
    // The fit uses the drawn content only. The stored world size can be smaller than the layout.
    if (contents) {
      view.contentW = 1;
      view.contentH = 1;
      kids.forEach(function (e) {
        view.contentW = Math.max(view.contentW, e.x + e.w);
        view.contentH = Math.max(view.contentH, e.y + e.h);
      });
      root.appendChild(node('rect', { x: 0, y: 0, width: view.contentW, height: view.contentH, 'class': 'plane' }));
      kids.forEach(function (e) {
        if (e.kind === 'sea') { drawTree(root, e); }
      });
      kids.forEach(function (e) {
        if (e.kind !== 'sea') { drawTree(root, e); }
      });
    } else {
      var grid = layoutTiles(kids.filter(function (e) { return e.kind !== 'sea'; }));
      view.contentW = Math.max(grid.w, 1);
      view.contentH = Math.max(grid.h, 1);
      root.appendChild(node('rect', { x: 0, y: 0, width: view.contentW, height: view.contentH, 'class': 'plane' }));
      kids.forEach(function (e) {
        var t = view.tiles[e.id];
        if (!t) { return; }
        var g = node('g', { 'data-id': e.id, transform: 'translate(' + t.x + ',' + t.y + ')' });
        g.appendChild(node('rect', {
          x: 0, y: 0, width: t.w, height: t.h, rx: 12,
          'class': 'el kind-' + e.kind + ' status-' + e.status
        }));
        g.appendChild(node('text', {
          x: TILE_PAD, y: 34, 'class': 'label', style: 'font-size:' + tileFont(e.name) + 'px'
        }, e.name));
        var inside = childCount(e.id);
        if (inside) { g.appendChild(node('text', { x: TILE_PAD, y: t.h - 12, 'class': 'badge' }, inside + ' inside')); }
        root.appendChild(g);
      });
    }
    drawRoads(root, contents);
    markHover();
    if (view.manual) { applyTransform(); } else { fitView(); }
  }

  function setMaximized(on) {
    stage.classList.toggle('atlas-max', on);
    document.body.classList.toggle('atlas-lock', on);
    if (maximizeButton) { maximizeButton.hidden = on; }
    if (closeButton) { closeButton.hidden = !on; }
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

  function hitId(target) {
    var hit = target && target.closest ? target.closest('[data-id]') : null;
    return hit ? hit.getAttribute('data-id') : '';
  }

  svg.addEventListener('pointerdown', function (ev) {
    var id = hitId(ev.target);
    var e = id ? find(id) : null;
    // Only the stored layout of a contents view can be edited; overview tiles pan the map.
    var moves = !!(e && contentsBox && contentsBox.checked && editable);
    drag = {
      id: id, moves: moves, startX: ev.clientX, startY: ev.clientY,
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
    view.manual = true;
    if (drag.moves) {
      var e = find(drag.id);
      e.x = Math.max(0, Math.round(drag.origX + dx / view.scale));
      e.y = Math.max(0, Math.round(drag.origY + dy / view.scale));
      draw();
    } else {
      view.tx = drag.tx0 + dx;
      view.ty = drag.ty0 + dy;
      applyTransform();
    }
  });

  svg.addEventListener('pointerup', function () {
    var d = drag;
    drag = null;
    if (!d || !d.id) { return; }
    var e = find(d.id);
    if (d.moved) {
      if (d.moves && (e.x !== d.origX || e.y !== d.origY)) { saveMove(e); }
      return;
    }
    if (childCount(e.id)) {
      enter(e.id);
    } else if (base) {
      window.location.href = base + '/element/' + encodeURIComponent(e.id);
    }
  });

  svg.addEventListener('pointerover', function (ev) { setHover(hitId(ev.target)); });
  svg.addEventListener('pointerleave', function () { setHover(''); });

  svg.addEventListener('wheel', function (ev) {
    ev.preventDefault();
    var rect = svg.getBoundingClientRect();
    zoomAt(ev.clientX - rect.left, ev.clientY - rect.top, ev.deltaY < 0 ? 1.15 : 1 / 1.15);
  }, { passive: false });

  if (upButton) {
    upButton.addEventListener('click', function () {
      var cur = view.plane ? find(view.plane) : null;
      enter(cur ? cur.parent : '');
    });
  }
  function onClick(id, fn) {
    var el = document.getElementById(id);
    if (el) { el.addEventListener('click', fn); }
  }
  onClick('atlas-zoom-in', function () { zoomCentre(1.25); });
  onClick('atlas-zoom-out', function () { zoomCentre(1 / 1.25); });
  onClick('atlas-fit', function () { view.manual = false; fitView(); });
  onClick('atlas-actual', actualSize);
  onClick('atlas-maximize', function () { setMaximized(true); });
  onClick('atlas-close', function () { setMaximized(false); });
  if (contentsBox) {
    contentsBox.addEventListener('change', function () { draw(); });
  }

  document.addEventListener('keydown', function (ev) {
    var tag = ev.target && ev.target.tagName ? ev.target.tagName : '';
    if (tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT') { return; }
    if (ev.key === 'Escape' && stage.classList.contains('atlas-max')) { setMaximized(false); return; }
    if (ev.key === '+' || ev.key === '=') { zoomCentre(1.25); }
    else if (ev.key === '-' || ev.key === '_') { zoomCentre(1 / 1.25); }
    else if (ev.key === '0') { view.manual = false; fitView(); }
  });

  window.addEventListener('hashchange', function () { readHash(); draw(); });
  window.addEventListener('resize', function () { draw(); });
  readHash();
  draw();
})();
)ATLAS";
}

END_UPP_NAMESPACE
