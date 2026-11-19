#include "MultiAtlasCanvas.h"

NAMESPACE_UPP

static Value MultiAtlasCanvasVersionsValue(const Vector<MultiAtlasVersion>& versions)
{
	ValueArray out;
	for(const MultiAtlasVersion& v : versions) {
		ValueMap row;
		row.Add("vid", v.vid);
		row.Add("code", v.code);
		row.Add("label", Format("%d.%d.%d", v.code / 1000000, (v.code / 1000) % 1000, v.code % 1000));
		out.Add(row);
	}
	return out;
}

Value MultiAtlasCanvasValue(const MultiAtlasCanvasData& data)
{
	ValueMap root;
	root.Add("ensemble", data.ensemble);
	root.Add("title", data.title);
	ValueArray lanes;
	for(const MultiAtlasCanvasLane& lane : data.lanes) {
		ValueMap item;
		item.Add("id", lane.id);
		item.Add("title", lane.title);
		item.Add("branch", lane.branch);
		item.Add("versions", MultiAtlasCanvasVersionsValue(lane.versions));
		ValueMap element_versions;
		for(int i = 0; i < lane.element_versions.GetCount(); i++)
			element_versions.Add(lane.element_versions.GetKey(i), lane.element_versions[i]);
		item.Add("element_versions", element_versions);
		ValueMap gates;
		for(int i = 0; i < lane.gate_status.GetCount(); i++) gates.Add(lane.gate_status.GetKey(i), lane.gate_status[i]);
		item.Add("gate_status", gates);
		ValueMap map;
		map.Add("width", lane.map.world.width);
		map.Add("height", lane.map.world.height);
		ValueArray elements;
		for(const MultiAtlasElement& e : lane.map.elements) {
			ValueMap el;
			el.Add("id", e.id); el.Add("name", e.name); el.Add("kind", e.kind);
			el.Add("status", e.status); el.Add("x", e.x); el.Add("y", e.y);
			el.Add("w", e.w); el.Add("h", e.h); el.Add("parent", e.parent);
			elements.Add(el);
		}
		map.Add("elements", elements);
		ValueArray roads;
		for(const MultiAtlasRoad& r : lane.map.roads) {
			ValueMap road;
			road.Add("from", r.from); road.Add("to", r.to);
			road.Add("from_track", r.from_track.IsEmpty() ? lane.id : r.from_track);
			road.Add("to_track", r.to_track.IsEmpty() ? lane.id : r.to_track);
			road.Add("kind", r.road_kind); road.Add("label", r.label);
			roads.Add(road);
		}
		map.Add("roads", roads);
		item.Add("map", map);
		lanes.Add(item);
	}
	root.Add("lanes", lanes);
	return root;
}

static String MultiAtlasCanvasHtmlEsc(const String& value)
{
	StringBuffer out;
	for(int i = 0; i < value.GetCount(); i++) switch(value[i]) {
	case '&': out << "&amp;"; break; case '<': out << "&lt;"; break;
	case '>': out << "&gt;"; break; case '"': out << "&quot;"; break;
	case '\'': out << "&#39;"; break; default: out.Cat(value[i]);
	}
	return String(out);
}

static String MultiAtlasCanvasSafeJson(const String& json)
{
	StringBuffer out;
	for(int i = 0; i < json.GetCount(); i++) {
		char c = json[i];
		if(c == '<') out << "\\u003c";
		else if(c == '>') out << "\\u003e";
		else if(c == '&') out << "\\u0026";
		else out.Cat(c);
	}
	return String(out);
}

String MultiAtlasCanvasHtml(const MultiAtlasCanvasData& data)
{
	String json = MultiAtlasCanvasSafeJson(AsJSON(MultiAtlasCanvasValue(data)));
	StringBuffer b;
	b << "<div class=\"ma-shell\">"
	  << "<div class=\"ma-tabs\" role=\"tablist\" aria-label=\"MultiAtlas maps\">"
	  << "<button type=\"button\" data-view=\"overview\" role=\"tab\" aria-selected=\"true\">Overview / Swimlanes</button>";
	for(const MultiAtlasCanvasLane& lane : data.lanes)
		b << "<button type=\"button\" data-view=\"" << MultiAtlasCanvasHtmlEsc(lane.id) << "\" role=\"tab\" aria-selected=\"false\">"
		  << MultiAtlasCanvasHtmlEsc(lane.title) << "</button>";
	b << "</div><div class=\"ma-detail-link\" hidden><a data-track-detail>Open track diagram</a></div>"
	  << "<div class=\"ma-external\" hidden aria-label=\"External dependencies\"></div>"
	  << "<div class=\"ma-toolbar\" aria-label=\"Canvas controls\">"
	  << "<button type=\"button\" data-action=\"in\" aria-label=\"Zoom in\">+</button>"
	  << "<button type=\"button\" data-action=\"out\" aria-label=\"Zoom out\">−</button>"
	  << "<button type=\"button\" data-action=\"fit\">Fit (0)</button>"
	  << "<button type=\"button\" data-action=\"width\" aria-pressed=\"false\">Fit width / Page scroll</button>"
	  << "<button type=\"button\" data-action=\"one\">1:1</button>"
	  << "<button type=\"button\" data-action=\"maximize\">Maximize</button>"
	  << "<button type=\"button\" data-action=\"theme\">Theme</button>"
	  << "<output class=\"ma-zoom\" aria-live=\"polite\">100%</output></div>"
	  << "<div class=\"ma-stage\"><svg class=\"ma-canvas\" role=\"img\" aria-label=\"MultiAtlas roadmap\" tabindex=\"0\"></svg></div>"
	  << "<script type=\"application/json\" class=\"ma-data\">" << json << "</script></div>";
	return String(b);
}

String MultiAtlasCanvasStylesheet()
{
	return R"MA(<style>
.ma-shell { --ma-lane: 250px; color: var(--fg); background: var(--panel); border: 1px solid var(--line); border-radius: 8px; overflow: hidden; }
.ma-tabs, .ma-toolbar { display: flex; flex-wrap: wrap; gap: 6px; padding: 8px; border-bottom: 1px solid var(--line); align-items: center; }
.ma-tabs button[aria-selected=true] { outline: 2px solid var(--accent); }
.ma-tabs button, .ma-toolbar button { font: inherit; color: var(--fg); background: var(--bg); border: 1px solid var(--line); border-radius: 5px; padding: 5px 10px; cursor: pointer; }
.ma-external { display: flex; flex-wrap: wrap; gap: 6px; align-items: center; padding: 6px 8px; border-bottom: 1px solid var(--line); }
.ma-external[hidden] { display: none; }
.ma-external button { font: inherit; color: var(--fg); background: var(--bg); border: 1px solid var(--accent); border-radius: 999px; padding: 3px 9px; cursor: pointer; }
.ma-external-label { color: var(--muted); font-size: .9em; }
.ma-detail-link { padding: 6px 8px; border-bottom: 1px solid var(--line); }
.ma-detail-link[hidden] { display: none; }
.ma-detail-link a { color: var(--accent); font-weight: 600; }
.ma-zoom { margin-left: auto; color: var(--muted); font-variant-numeric: tabular-nums; }
.ma-stage { height: 72vh; min-height: 400px; overflow: hidden; touch-action: none; }
.ma-canvas { width: 100%; height: 100%; display: block; cursor: grab; }
.ma-canvas:active { cursor: grabbing; }
.ma-shell.ma-fit-width .ma-stage { min-height: 0; touch-action: auto; }
.ma-shell.ma-fit-width .ma-canvas { cursor: auto; }
.ma-toolbar button[aria-pressed=true] { border-color: var(--accent); box-shadow: inset 0 0 0 1px var(--accent); }
.ma-canvas .lane-bg { fill: var(--bg); stroke: var(--line); }
.ma-canvas .band { fill: var(--panel); stroke: var(--line); }
.ma-canvas .lane-title, .ma-canvas .band-label { fill: var(--fg); font: 600 15px system-ui, sans-serif; }
.ma-canvas .minor-label { fill: var(--muted); font: 600 13px system-ui, sans-serif; }
.ma-canvas .element { fill: var(--panel); stroke: var(--accent); stroke-width: 2; }
.ma-canvas .element.status-active { stroke: var(--st-active); }
.ma-canvas .element.status-settled { stroke: var(--st-settled); }
.ma-canvas .element.status-trouble { stroke: var(--st-trouble); }
.ma-canvas .element-label { fill: var(--fg); font: 13px system-ui, sans-serif; pointer-events: none; }
.ma-canvas .road { fill: none; stroke: var(--st-hot); stroke-width: 2; stroke-dasharray: 7 5; pointer-events: none; }
.ma-canvas .road.local { stroke: var(--muted); stroke-dasharray: none; }
.ma-canvas .road-hit { fill: none; stroke: transparent; stroke-width: 14; pointer-events: stroke; cursor: pointer; }
.ma-canvas .road-label { fill: var(--fg); font: 12px system-ui, sans-serif; pointer-events: none; }
.ma-canvas g[data-id] { cursor: pointer; }
.ma-canvas g.highlight .element { stroke: var(--st-hot); stroke-width: 4; }
.ma-shell.ma-max { position: fixed; inset: 0; z-index: 1200; border-radius: 0; display: flex; flex-direction: column; }
.ma-shell.ma-max .ma-stage { flex: 1; min-height: 0; }
.ma-shell.ma-contrast { --bg: #101820; --panel: #182530; --fg: #fff; --muted: #d0d9e2; --line: #718092; --accent: #ffe66d; --st-hot: #ffcf4a; --st-active: #66e0c2; --st-settled: #a7d8ff; --st-trouble: #ff8a8a; }
body.ma-lock { overflow: hidden; }
@media (max-width: 700px) {
  .ma-stage { height: 68vh; min-height: 340px; }
  .ma-tabs { overflow-x: auto; flex-wrap: nowrap; }
  .ma-tabs button { white-space: nowrap; }
}
@media (prefers-reduced-motion: reduce) { .ma-canvas { scroll-behavior: auto; } }
</style>)MA";
}

String MultiAtlasCanvasScript()
{
	return R"MA(<script>
(function () {
  'use strict';
  var NS = 'http://www.w3.org/2000/svg';
  document.querySelectorAll('.ma-shell').forEach(function (shell) {
    var data = JSON.parse(shell.querySelector('.ma-data').textContent);
    var svg = shell.querySelector('.ma-canvas');
    var stage = shell.querySelector('.ma-stage');
    var zoom = shell.querySelector('.ma-zoom');
    var widthMode = shell.querySelector('[data-action=width]');
    var maximize = shell.querySelector('[data-action=maximize]');
    var panZoomControls = shell.querySelectorAll('[data-action=in], [data-action=out], [data-action=one], [data-action=maximize]');
    var external = shell.querySelector('.ma-external');
    var detailLink = shell.querySelector('.ma-detail-link');
    var detailAnchor = shell.querySelector('[data-track-detail]');
    var lanes = data.lanes || [];
    var view = { x: 0, y: 0, scale: 1, mode: 'overview', fitWidth: false };
    var drag = null;
    var camera = null;
    var contentWidth = 1;
    var contentHeight = 1;
    var fittedWidth = 0;

    function node(name, attrs, text) {
      var out = document.createElementNS(NS, name);
      Object.keys(attrs || {}).forEach(function (key) { out.setAttribute(key, attrs[key]); });
      if (text !== undefined) out.textContent = text;
      return out;
    }
    function add(parent, name, attrs, text) {
      var out = node(name, attrs, text);
      parent.appendChild(out);
      return out;
    }
    function minor(code) {
      return Math.floor(code / 1000000) + '.' + Math.floor(code / 1000) % 1000;
    }
    function selectedLanes() {
      return view.mode === 'overview' ? lanes : lanes.filter(function (lane) { return lane.id === view.mode; });
    }
    function render() {
      while (svg.firstChild) svg.removeChild(svg.firstChild);
      var selected = selectedLanes(), groups = Object.create(null), positions = Object.create(null);
      selected.forEach(function (lane) {
        (lane.versions || []).forEach(function (version) { groups[minor(version.code)] = 0; });
      });
      var rows = Object.keys(groups).sort(function (a, b) {
        var av = a.split('.').map(Number), bv = b.split('.').map(Number);
        return av[0] - bv[0] || av[1] - bv[1];
      });
      selected.forEach(function (lane) {
        var counts = Object.create(null);
        (lane.map.elements || []).forEach(function (element) {
          var vid = lane.element_versions && lane.element_versions[element.id];
          var version = (lane.versions || []).filter(function (item) { return item.vid === vid; })[0];
          var group = version ? minor(version.code) : rows[0];
          counts[group] = (counts[group] || 0) + 1;
        });
        Object.keys(counts).forEach(function (group) { groups[group] = Math.max(groups[group] || 0, counts[group]); });
      });
      var laneWidth = 250, top = 42, rowHeights = [], rowTops = [], height = top;
      rows.forEach(function (group, index) {
        rowTops[index] = height;
        rowHeights[index] = Math.max(76, 42 + (groups[group] || 0) * 40);
        height += rowHeights[index];
      });
      height += 20;
      var width = Math.max(1, selected.length) * laneWidth + 32;
      contentWidth = width;
      contentHeight = height;
      var root = add(svg, 'g', {});
      camera = root;
      selected.forEach(function (lane, index) {
        var x = index * laneWidth;
        add(root, 'rect', { x: x, y: 0, width: laneWidth - 8, height: height, class: 'lane-bg', rx: 5 });
        add(root, 'text', { x: x + 12, y: 28, class: 'lane-title' }, lane.title + (lane.branch ? ' · ' + lane.branch : ''));
        rows.forEach(function (group, row) {
          var y = rowTops[row];
          add(root, 'rect', { x: x + 1, y: y, width: laneWidth - 10, height: rowHeights[row] - 4, class: 'band', rx: 3 });
          if (index === 0) add(root, 'text', { x: x + 8, y: y + 19, class: 'minor-label' }, group);
          var gate = lane.gate_status && lane.gate_status[group];
          if (gate) add(root, 'text', { x: x + laneWidth - 92, y: y + 19, class: 'minor-label gate-' + gate.toLowerCase() }, 'Gate ' + gate);
        });
        (lane.map.elements || []).forEach(function (element) {
          var vid = lane.element_versions && lane.element_versions[element.id];
          var version = (lane.versions || []).filter(function (item) { return item.vid === vid; })[0];
          var row = version ? rows.indexOf(minor(version.code)) : 0;
          row = Math.max(0, row);
          var groupName = version ? minor(version.code) : rows[0];
          var peerCount = (lane.map.elements || []).filter(function (candidate) {
            var peerVid = lane.element_versions && lane.element_versions[candidate.id];
            var peerVersion = (lane.versions || []).filter(function (item) { return item.vid === peerVid; })[0];
            return (peerVersion ? minor(peerVersion.code) : rows[0]) === groupName && candidate.id < element.id;
          }).length;
          var y = rowTops[row] + 32 + peerCount * 40;
          var w = Math.min(laneWidth - 30, Math.max(90, element.w || 120));
          var groupNode = add(root, 'g', { 'data-id': element.id, 'data-lane': lane.id, tabindex: '0', role: 'button' });
          add(groupNode, 'rect', { x: x + 10, y: y, width: w, height: 30, rx: 5, class: 'element ' + (element.status || '') });
          add(groupNode, 'text', { x: x + 18, y: y + 20, class: 'element-label' }, element.name || element.id);
          positions[element.id + '@' + lane.id] = { x: x + 10 + w / 2, y: y + 16, w: w, lane: lane.id };
          groupNode.addEventListener('click', function () { highlight(element.id, lane.id); });
          groupNode.addEventListener('keydown', function (event) {
            if (event.key === 'Enter' || event.key === ' ') { event.preventDefault(); highlight(element.id, lane.id); }
          });
        });
      });
      selected.forEach(function (lane) {
        (lane.map.roads || []).forEach(function (road) {
          var fromLane = road.from_track || lane.id, toLane = road.to_track || lane.id;
          var from = positions[road.from + '@' + fromLane], to = positions[road.to + '@' + toLane];
          if (!from || !to) return;
          var bend = Math.max(25, Math.abs(to.x - from.x) * .45);
          var pathAttrs = {
            d: 'M' + from.x + ' ' + from.y + ' C' + (from.x + bend) + ' ' + from.y + ' ' + (to.x - bend) + ' ' + to.y + ' ' + to.x + ' ' + to.y
          };
          var hit = add(root, 'path', Object.assign({}, pathAttrs, {
            class: 'road-hit', tabindex: '0', role: 'link', 'aria-label': road.label || 'Dependency road'
          }));
          add(root, 'path', Object.assign({}, pathAttrs, { class: 'road ' + (from.lane === to.lane ? 'local' : '') }));
          if (road.label && from.lane !== to.lane)
            add(root, 'text', { x: (from.x + to.x) / 2, y: (from.y + to.y) / 2 - 5, class: 'road-label' }, road.label);
          function navigate() {
            if (to.lane !== 'meta') { view.mode = to.lane; updateTabs(); fit(); }
            window.setTimeout(function () { highlight(road.to, to.lane); }, 0);
          }
          hit.addEventListener('click', navigate);
          hit.addEventListener('keydown', function (event) {
            if (event.key === 'Enter' || event.key === ' ') { event.preventDefault(); navigate(); }
          });
        });
      });
      svg.querySelectorAll('g[data-id]').forEach(function (element) { root.appendChild(element); });
      transform();
    }
    function transform() {
      if (camera) camera.setAttribute('transform', 'translate(' + view.x + ' ' + view.y + ') scale(' + view.scale + ')');
      zoom.textContent = Math.round(view.scale * 100) + '%';
    }
    function renderExternalLinks() {
      while (external.firstChild) external.removeChild(external.firstChild);
      if (view.mode === 'overview') { external.hidden = true; return; }
      var lane = lanes.filter(function (item) { return item.id === view.mode; })[0];
      if (!lane) { external.hidden = true; return; }
      var dependencies = (lane.map.roads || []).filter(function (road) {
        return (road.from_track || lane.id) !== (road.to_track || lane.id);
      });
      external.hidden = dependencies.length === 0;
      if (!dependencies.length) return;
      var caption = document.createElement('span');
      caption.className = 'ma-external-label';
      caption.textContent = 'External dependencies:';
      external.appendChild(caption);
      dependencies.forEach(function (road) {
        var fromLane = road.from_track || lane.id, toLane = road.to_track || lane.id;
        var targetLane = fromLane === lane.id ? toLane : fromLane;
        var targetId = fromLane === lane.id ? road.to : road.from;
        var button = document.createElement('button');
        button.type = 'button';
        button.title = 'Open ' + targetLane + ' dependency endpoint';
        button.textContent = (road.label || 'dependency') + ' → ' + targetLane;
        button.addEventListener('click', function () {
          view.mode = targetLane;
          updateTabs();
          fit();
          window.setTimeout(function () { highlight(targetId, targetLane); }, 0);
        });
        external.appendChild(button);
      });
    }
    function updateTabs() {
      shell.querySelectorAll('[data-view]').forEach(function (button) {
        button.setAttribute('aria-selected', button.dataset.view === view.mode ? 'true' : 'false');
      });
      detailLink.hidden = view.mode === 'overview';
      if (view.mode !== 'overview') {
        detailAnchor.href = '/multiatlas/' + encodeURIComponent(data.ensemble) +
          '/track/' + encodeURIComponent(view.mode);
      }
      renderExternalLinks();
      render();
    }
    function fit() {
      var width = Math.max(1, stage.clientWidth);
      fittedWidth = width;
      if (view.fitWidth) {
        view.scale = width / contentWidth;
        stage.style.height = Math.ceil(contentHeight * view.scale + 24) + 'px';
      } else {
        stage.style.height = '';
        view.scale = Math.max(.08, Math.min(1.7, Math.min(width / contentWidth, stage.clientHeight / contentHeight)));
      }
      svg.setAttribute('viewBox', '0 0 ' + width + ' ' + Math.max(1, stage.clientHeight));
      view.x = (width - contentWidth * view.scale) / 2;
      view.y = 12;
      transform();
    }
    function setMaximized(active) {
      shell.classList.toggle('ma-max', active);
      document.body.classList.toggle('ma-lock', active);
      maximize.textContent = active ? 'Restore' : 'Maximize';
    }
    function toggleFitWidth() {
      view.fitWidth = !view.fitWidth;
      drag = null;
      if (shell.classList.contains('ma-max')) setMaximized(false);
      shell.classList.toggle('ma-fit-width', view.fitWidth);
      widthMode.setAttribute('aria-pressed', String(view.fitWidth));
      panZoomControls.forEach(function (control) { control.disabled = view.fitWidth; });
      fit();
    }
    function highlight(id, lane) {
      var target = svg.querySelector('[data-id="' + CSS.escape(id) + '"][data-lane="' + CSS.escape(lane) + '"]');
      if (!target) return;
      target.classList.add('highlight');
      target.focus({ preventScroll: !view.fitWidth });
      window.setTimeout(function () { target.classList.remove('highlight'); }, 1700);
    }
    shell.querySelectorAll('[data-view]').forEach(function (button) {
      button.addEventListener('click', function () { view.mode = button.dataset.view; updateTabs(); fit(); });
    });
    shell.querySelectorAll('[data-action]').forEach(function (button) {
      button.addEventListener('click', function () {
        var action = button.dataset.action;
        if (action === 'in' || action === 'out') { view.scale *= action === 'in' ? 1.2 : 1 / 1.2; transform(); }
        else if (action === 'one') { view.scale = 1; view.x = 0; view.y = 0; transform(); }
        else if (action === 'fit') fit();
        else if (action === 'width') toggleFitWidth();
        else if (action === 'maximize') {
          setMaximized(!shell.classList.contains('ma-max'));
          window.setTimeout(fit, 60);
        }
        else if (action === 'theme') shell.classList.toggle('ma-contrast');
      });
    });
    svg.addEventListener('wheel', function (event) {
      if (view.fitWidth) return;
      event.preventDefault(); view.scale *= event.deltaY < 0 ? 1.12 : 1 / 1.12; transform();
    }, { passive: false });
    svg.addEventListener('pointerdown', function (event) {
      if (view.fitWidth) return;
      drag = { x: event.clientX, y: event.clientY, ox: view.x, oy: view.y };
    });
    svg.addEventListener('pointermove', function (event) {
      if (!drag) return;
      view.x = drag.ox + event.clientX - drag.x; view.y = drag.oy + event.clientY - drag.y;
      transform();
    });
    svg.addEventListener('pointerup', function () { drag = null; });
    svg.addEventListener('pointercancel', function () { drag = null; });
    svg.addEventListener('keydown', function (event) {
      if (event.key === '0') fit();
      else if (view.fitWidth) return;
      else if (event.key === '+' || event.key === '=') { view.scale *= 1.2; transform(); }
      else if (event.key === '-') { view.scale /= 1.2; transform(); }
    });
    window.addEventListener('resize', fit);
    render(); fit();
    new ResizeObserver(function () {
      if (view.fitWidth && stage.clientWidth !== fittedWidth) fit();
    }).observe(stage);
    var focus = /track=([^&]+)&element=([^&]+)/.exec(window.location.hash);
    if (focus) {
      try {
        var focusTrack = decodeURIComponent(focus[1]);
        var focusElement = decodeURIComponent(focus[2]);
        if (lanes.some(function (lane) { return lane.id === focusTrack; })) {
          view.mode = focusTrack;
          updateTabs();
          fit();
          window.setTimeout(function () { highlight(focusElement, focusTrack); }, 0);
        }
      } catch (ignore) { /* malformed fragment is ignored */ }
    }
  });
})();
</script>)MA";
}

END_UPP_NAMESPACE
