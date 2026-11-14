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
#atlas-canvas g[data-id] { transition: none; }
#atlas-canvas g.flash-error > rect.el { stroke: var(--st-trouble); stroke-width: 3; animation: atlas-flash 1.2s ease-in-out infinite; }
#atlas-canvas g.flash-warn > rect.el { stroke: var(--st-hot); stroke-width: 3; }
@keyframes atlas-flash { 0%, 100% { stroke-opacity: 1; } 50% { stroke-opacity: .25; } }
.atlas-player { align-items: center; }
.atlas-player input[type=range] { width: min(240px, 100%); }
@media (prefers-reduced-motion: reduce) { #atlas-canvas g.flash-error > rect.el { animation: none; } }
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
  var tightBox = document.getElementById('atlas-tight');
  var edgesBox = document.getElementById('atlas-edges');
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
               contentW: 1, contentH: 1, hover: '', tight: false, edges: false, packed: {} };
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

  // Rect of an element for the current view: the stored rect, or the packed rect when empty space is removed.
  function rectOf(e) {
    var p = view.tight ? view.packed[e.id] : null;
    return p ? p : { x: e.x, y: e.y, w: e.w, h: e.h };
  }

  // Position of an element relative to the current plane, summing the offsets of its parents.
  function absPos(e) {
    var r = rectOf(e), x = r.x, y = r.y, p = e.parent ? find(e.parent) : null;
    while (p && p.id !== view.plane) {
      var q = rectOf(p);
      x += q.x;
      y += q.y;
      p = p.parent ? find(p.parent) : null;
    }
    return { x: x, y: y, w: r.w, h: r.h };
  }

  // Empty space removal: each block is sized to its children, and siblings are packed in rows.
  var PACK_GAP = 14, PACK_TITLE = 34;

  function packKids(kids, origin) {
    var sizes = kids.map(function (c) { return sizeOf(c); });
    var maxW = 0, total = 0;
    sizes.forEach(function (s) { maxW = Math.max(maxW, s.w); total += s.w * s.h; });
    var target = Math.max(maxW, Math.sqrt(total) * 1.3);
    var x = 0, y = 0, rowH = 0, right = 0;
    kids.forEach(function (c, i) {
      var s = sizes[i];
      if (x > 0 && x + s.w > target) { x = 0; y += rowH + PACK_GAP; rowH = 0; }
      view.packed[c.id] = { x: origin.x + x, y: origin.y + y, w: s.w, h: s.h };
      x += s.w + PACK_GAP;
      rowH = Math.max(rowH, s.h);
      right = Math.max(right, x - PACK_GAP);
    });
    return { w: origin.x + right, h: origin.y + y + rowH };
  }

  function sizeOf(e) {
    var kids = childrenOf(e.id);
    if (!kids.length) { return { w: e.w, h: e.h }; }
    var inner = packKids(kids, { x: PACK_GAP, y: PACK_TITLE });
    return { w: Math.max(inner.w + PACK_GAP, e.name.length * 9 + 28), h: inner.h + PACK_GAP };
  }

  function drawTree(parentG, e) {
    var r = rectOf(e);
    var g = node('g', { 'data-id': e.id, transform: 'translate(' + r.x + ',' + r.y + ')' });
    if (e.kind === 'sea') {
      g.appendChild(node('rect', { x: 0, y: 0, width: r.w, height: r.h, rx: 24, 'class': 'sea' }));
    } else {
      g.appendChild(node('rect', {
        x: 0, y: 0, width: r.w, height: r.h, rx: 12,
        'class': 'el kind-' + e.kind + ' status-' + e.status
      }));
    }
    var fs = titleFont(r);
    g.appendChild(node('text', { x: 12, y: fs + 6, 'class': 'label', style: 'font-size:' + fs + 'px' }, e.name));
    var inside = childCount(e.id);
    if (inside && r.h >= 60) {
      g.appendChild(node('text', { x: 12, y: r.h - 10, 'class': 'badge' }, inside + ' inside'));
    }
    childrenOf(e.id).forEach(function (c) { drawTree(g, c); });
    parentG.appendChild(g);
  }

  // The drawn box of a road end: the block itself in contents mode, the tile holding it in the overview.
  function roadBox(id, contents) {
    var e = find(id);
    if (!e) { return null; }
    if (contents) {
      if (!inPlane(id)) { return null; }
      var p = absPos(e);
      return { id: e.id, x: p.x, y: p.y, w: p.w, h: p.h };
    }
    var top = inPlane(id);
    var t = top ? view.tiles[top.id] : null;
    return t ? { id: top.id, x: t.x, y: t.y, w: t.w, h: t.h } : null;
  }

  // Side of a box that faces a point (edge mode), by the direction from the box centre.
  function sideOf(box, ox, oy) {
    var dx = ox - (box.x + box.w / 2), dy = oy - (box.y + box.h / 2);
    if (Math.abs(dx) * box.h >= Math.abs(dy) * box.w) { return dx >= 0 ? 'right' : 'left'; }
    return dy >= 0 ? 'bottom' : 'top';
  }

  // Road ends: each block side (or the centre, when edge mode is off) spreads its ends, so no two lines share one point.
  function roadEnds(list) {
    var groups = {};
    list.forEach(function (ro, i) {
      [['a', ro.a, ro.b], ['b', ro.b, ro.a]].forEach(function (part) {
        var box = part[1], other = part[2];
        var ox = other.x + other.w / 2, oy = other.y + other.h / 2;
        var side = view.edges ? sideOf(box, ox, oy) : 'centre';
        var key = box.id + '|' + side;
        (groups[key] = groups[key] || []).push({ i: i, which: part[0], box: box, side: side, ox: ox, oy: oy });
      });
    });
    var points = {};
    Object.keys(groups).forEach(function (key) {
      var g = groups[key];
      var vertical = g[0].side === 'top' || g[0].side === 'bottom';
      g.sort(function (p, q) { return vertical || g[0].side === 'centre' ? p.ox - q.ox : p.oy - q.oy; });
      var n = g.length;
      g.forEach(function (p, k) {
        var b = p.box, t = (k + 1) / (n + 1), pt;
        if (p.side === 'top') { pt = { x: b.x + t * b.w, y: b.y }; }
        else if (p.side === 'bottom') { pt = { x: b.x + t * b.w, y: b.y + b.h }; }
        else if (p.side === 'left') { pt = { x: b.x, y: b.y + t * b.h }; }
        else if (p.side === 'right') { pt = { x: b.x + b.w, y: b.y + t * b.h }; }
        else {
          var step = Math.min(16, (b.w * 0.8) / n);
          pt = { x: b.x + b.w / 2 + (k - (n - 1) / 2) * step, y: b.y + b.h / 2 };
        }
        points[p.i + p.which] = pt;
      });
    });
    return points;
  }

  function drawRoads(root, contents) {
    var list = [], seen = {};
    map.roads.forEach(function (r) {
      var a = roadBox(r.from, contents);
      var b = roadBox(r.to, contents);
      if (!a || !b || a.id === b.id) { return; }
      var key = a.id + '>' + b.id;
      if (seen[key]) { return; }
      seen[key] = true;
      list.push({ r: r, a: a, b: b });
    });
    var points = roadEnds(list);
    list.forEach(function (ro, i) {
      var p = points[i + 'a'], q = points[i + 'b'];
      root.appendChild(node('path', {
        d: 'M' + p.x + ' ' + p.y + ' L' + q.x + ' ' + q.y,
        'class': 'road kind-' + ro.r.kind,
        'data-a': ro.a.id, 'data-b': ro.b.id, 'data-road': ro.r.id
      }));
      if (ro.r.label) {
        root.appendChild(node('text', { x: (p.x + q.x) / 2, y: (p.y + q.y) / 2 - 4, 'class': 'road-label' }, ro.r.label));
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
    view.tight = contents && !!(tightBox && tightBox.checked);
    view.edges = !!(edgesBox && edgesBox.checked);
    var root = node('g', {});
    svg.appendChild(root);
    view.root = root;
    var kids = childrenOf(view.plane);
    view.tiles = {};
    view.packed = {};
    // The fit uses the drawn content only. The stored world size can be smaller than the layout.
    if (contents) {
      if (view.tight) {
        var packed = packKids(kids, { x: PACK_GAP, y: PACK_GAP });
        view.contentW = Math.max(packed.w + PACK_GAP, 1);
        view.contentH = Math.max(packed.h + PACK_GAP, 1);
      } else {
        view.contentW = 1;
        view.contentH = 1;
        kids.forEach(function (e) {
          view.contentW = Math.max(view.contentW, e.x + e.w);
          view.contentH = Math.max(view.contentH, e.y + e.h);
        });
      }
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
    paintFrame();
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
    var moves = !!(e && contentsBox && contentsBox.checked && !(tightBox && tightBox.checked) && editable);
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
  // The three view options are remembered per browser. Storage may be blocked; then they start unchecked.
  var VIEW_KEY = 'atlas-view';
  function readView() {
    try {
      var saved = JSON.parse(window.localStorage.getItem(VIEW_KEY) || '{}');
      return saved && typeof saved === 'object' ? saved : {};
    } catch (e) { return {}; }
  }
  function writeView() {
    var saved = {};
    [['contents', contentsBox], ['tight', tightBox], ['edges', edgesBox]].forEach(function (pair) {
      if (pair[1]) { saved[pair[0]] = !!pair[1].checked; }
    });
    try { window.localStorage.setItem(VIEW_KEY, JSON.stringify(saved)); } catch (e) { /* not remembered */ }
  }
  var savedView = readView();
  [['contents', contentsBox], ['tight', tightBox], ['edges', edgesBox]].forEach(function (pair) {
    if (pair[1] && typeof savedView[pair[0]] === 'boolean') { pair[1].checked = savedView[pair[0]]; }
  });
  [contentsBox, tightBox, edgesBox].forEach(function (box) {
    if (box) { box.addEventListener('change', function () { writeView(); draw(); }); }
  });

  // Listeners on document and window outlive this page's elements; they are removed when the shell
  // swaps the page content in place, so a later page does not inherit them.
  var detach = [];
  function listen(target, type, fn) {
    target.addEventListener(type, fn);
    detach.push(function () { target.removeEventListener(type, fn); });
  }
  if (window.AdShell) { window.AdShell.onLeave(function () { detach.forEach(function (off) { off(); }); }); }

  listen(document, 'keydown', function (ev) {
    var tag = ev.target && ev.target.tagName ? ev.target.tagName : '';
    if (tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT') { return; }
    if (ev.key === 'Escape' && stage.classList.contains('atlas-max')) { setMaximized(false); return; }
    if (ev.key === '+' || ev.key === '=') { zoomCentre(1.25); }
    else if (ev.key === '-' || ev.key === '_') { zoomCentre(1 / 1.25); }
    else if (ev.key === '0') { view.manual = false; fitView(); }
  });

  listen(window, 'hashchange', function () { readHash(); draw(); });
  listen(window, 'resize', function () { draw(); });
  // ---- animation: each version is a frame of the map. A critically damped spring moves the playback
  // position toward the chosen version; every drawn subject follows its presence and status at that
  // position, and roads draw in as they appear. Reduced motion jumps between versions.
  var framesBox = document.getElementById('atlas-frames');
  var frames = null;
  try { frames = framesBox ? JSON.parse(framesBox.textContent) : null; } catch (e) { frames = null; }
  var OMEGA = 7;
  var STEP_MS = 1400;
  var reduced = !!(window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches);
  var motion = { t: 0, v: 0, target: 0, playing: false, speed: 1, next: 0, raf: 0, last: 0, count: 0, tracks: {} };

  // The pure parts, exposed for tests: a smooth step, the spring, and sampling a per-frame list.
  function smooth(f) { return f * f * (3 - 2 * f); }
  function spring(x, v, target, dt, omega) {
    var a = omega * omega * (target - x) - 2 * omega * v;
    var nv = v + a * dt;
    return { x: x + nv * dt, v: nv };
  }
  function sample(values, t) {
    var n = values.length;
    if (!n) { return 0; }
    if (t <= 0) { return values[0]; }
    if (t >= n - 1) { return values[n - 1]; }
    var i = Math.floor(t);
    return values[i] + (values[i + 1] - values[i]) * smooth(t - i);
  }
  window.AtlasMotion = { smooth: smooth, spring: spring, sample: sample };

  // One track per subject: presence and status for every frame, and the flag (error or warning) it carries.
  function buildTracks() {
    var n = frames.versions.length;
    motion.count = n;
    frames.versions.forEach(function (ver, k) {
      (ver.subjects || []).forEach(function (s) {
        var key = s.kind + ':' + s.id;
        var tr = motion.tracks[key] || (motion.tracks[key] = { present: [], status: [], flag: [] });
        tr.present[k] = s.present ? 1 : 0;
        tr.status[k] = s.status || '';
      });
      (ver.flags || []).forEach(function (f) {
        var tr = motion.tracks[f.kind + ':' + f.id];
        if (tr) { tr.flag[k] = f.severity; }
      });
    });
    Object.keys(motion.tracks).forEach(function (key) {
      var tr = motion.tracks[key];
      for (var k = 0; k < n; k++) {
        if (tr.present[k] === undefined) { tr.present[k] = 0; }
        if (tr.status[k] === undefined) { tr.status[k] = ''; }
        if (tr.flag[k] === undefined) { tr.flag[k] = ''; }
      }
    });
  }

  function paintFrame() {
    if (!motion.count) { return; }
    var k = Math.max(0, Math.min(motion.count - 1, Math.round(motion.t)));
    var ver = frames.versions[k];
    svg.querySelectorAll('g[data-id]').forEach(function (g) {
      var tr = motion.tracks['element:' + g.getAttribute('data-id')];
      if (!tr) { return; }
      var p = sample(tr.present, motion.t);
      g.style.opacity = String(p);
      g.style.pointerEvents = p < 0.5 ? 'none' : '';
      var rect = g.querySelector(':scope > rect.el');
      if (rect) {
        rect.setAttribute('class', rect.getAttribute('class').replace(/status-\S*/, 'status-' + (tr.status[k] || 'planned')));
      }
      g.classList.toggle('flash-error', tr.flag[k] === 'error');
      g.classList.toggle('flash-warn', tr.flag[k] === 'warning');
    });
    svg.querySelectorAll('path[data-road]').forEach(function (path) {
      var tr = motion.tracks['road:' + path.getAttribute('data-road')];
      if (!tr) { return; }
      var p = sample(tr.present, motion.t);
      if (p >= 0.999) {
        path.style.strokeDasharray = '';
        path.style.strokeDashoffset = '';
        path.style.opacity = '';
        return;
      }
      var len = path.getTotalLength();
      path.style.strokeDasharray = len + ' ' + len;
      path.style.strokeDashoffset = String((1 - p) * len);
      path.style.opacity = p > 0.02 ? '1' : '0';
    });
    var errors = 0, warnings = 0;
    (ver.flags || []).forEach(function (f) { if (f.severity === 'error') { errors++; } else { warnings++; } });
    var label = document.getElementById('atlas-frame-label');
    if (label) { label.textContent = ver.label + (k === frames.head ? ' (head)' : ''); }
    var flagged = document.getElementById('atlas-frame-errors');
    if (flagged) { flagged.textContent = errors + ' errors, ' + warnings + ' warnings at this version'; }
    var world = document.getElementById('atlas-world');
    if (world) { world.textContent = (ver.world || []).length ? ver.world.join(' \u00b7 ') : 'no atoms set'; }
    var scrub = document.getElementById('atlas-scrub');
    if (scrub && Number(scrub.value) !== k) { scrub.value = String(k); }
  }

  function step(now) {
    var dt = Math.min(0.05, Math.max(0, (now - motion.last) / 1000));
    motion.last = now;
    if (motion.playing && now >= motion.next) {
      motion.target = motion.target >= motion.count - 1 ? 0 : motion.target + 1;
      if (motion.target === 0) { motion.t = 0; motion.v = 0; }
      motion.next = now + STEP_MS / motion.speed;
    }
    if (reduced) {
      motion.t = motion.target;
      motion.v = 0;
    } else {
      var s = spring(motion.t, motion.v, motion.target, dt, OMEGA);
      motion.t = s.x;
      motion.v = s.v;
    }
    paintFrame();
    var settled = Math.abs(motion.target - motion.t) < 0.001 && Math.abs(motion.v) < 0.001;
    if (motion.playing || !settled) {
      motion.raf = requestAnimationFrame(step);
    } else {
      motion.t = motion.target;
      motion.v = 0;
      motion.raf = 0;
      paintFrame();
    }
  }
  function go(target) {
    motion.target = Math.max(0, Math.min(motion.count - 1, target));
    if (!motion.raf) {
      motion.last = performance.now();
      motion.raf = requestAnimationFrame(step);
    }
  }

  var playButton = document.getElementById('atlas-play');
  var scrubBox = document.getElementById('atlas-scrub');
  var speedBox = document.getElementById('atlas-speed');
  if (frames && frames.versions && frames.versions.length > 1) {
    buildTracks();
    motion.t = motion.target = frames.start;
    if (playButton) {
      playButton.addEventListener('click', function () {
        motion.playing = !motion.playing;
        playButton.textContent = motion.playing ? 'Pause' : 'Play';
        motion.next = performance.now();
        go(motion.target);
      });
    }
    if (scrubBox) {
      scrubBox.addEventListener('input', function () {
        motion.playing = false;
        if (playButton) { playButton.textContent = 'Play'; }
        go(Number(scrubBox.value));
      });
    }
    if (speedBox) {
      speedBox.addEventListener('change', function () { motion.speed = Number(speedBox.value) || 1; });
    }
    // Stop the loop when the shell swaps the page away.
    detach.push(function () {
      motion.playing = false;
      cancelAnimationFrame(motion.raf);
      motion.raf = 0;
    });
  }

  readHash();
  draw();
})();
)ATLAS";
}

END_UPP_NAMESPACE
