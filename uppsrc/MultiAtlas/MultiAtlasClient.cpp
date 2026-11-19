#include "MultiAtlasClient.h"

NAMESPACE_UPP

namespace {

static void AddValue(ValueMap& out, const char *key, const String& value) { out.Add(key, value); }

static Value MultiAtlasDetailValue(const MultiAtlasTrackDetail& detail)
{
	ValueMap root;
	AddValue(root, "ensemble", detail.ensemble);
	AddValue(root, "track", detail.track_id);
	AddValue(root, "version", detail.version);
	AddValue(root, "gate", detail.gate);
	ValueMap map;
	map.Add("width", detail.map.world.width);
	map.Add("height", detail.map.world.height);
	ValueArray elements;
	for(const MultiAtlasElement& e : detail.map.elements) {
		ValueMap item;
		AddValue(item, "id", e.id); AddValue(item, "name", e.name);
		AddValue(item, "kind", e.kind); AddValue(item, "status", e.status);
		AddValue(item, "parent", e.parent); AddValue(item, "notes", e.notes);
		item.Add("x", e.x); item.Add("y", e.y); item.Add("w", e.w); item.Add("h", e.h);
		item.Add("goal_picked", e.goal.picked); AddValue(item, "goal_note", e.goal.note);
		elements.Add(item);
	}
	map.Add("elements", elements);
	ValueArray roads;
	for(const MultiAtlasRoad& r : detail.map.roads) {
		ValueMap item;
		AddValue(item, "from", r.from); AddValue(item, "to", r.to);
		AddValue(item, "from_track", r.from_track.IsEmpty() ? detail.track_id : r.from_track);
		AddValue(item, "to_track", r.to_track.IsEmpty() ? detail.track_id : r.to_track);
		AddValue(item, "kind", r.road_kind); AddValue(item, "label", r.label);
		roads.Add(item);
	}
	map.Add("roads", roads);
	root.Add("map", map);
	ValueArray initial;
	for(const String& atom : detail.rules.initial) initial.Add(atom);
	root.Add("initial", initial);
	ValueArray atoms;
	for(const MultiAtlasAtomDef& atom : detail.rules.atoms) {
		ValueMap item; AddValue(item, "name", atom.name); AddValue(item, "text", atom.text);
		item.Add("gate", atom.gate); item.Add("export", atom.exported);
		atoms.Add(item);
	}
	root.Add("atoms", atoms);
	ValueArray versions;
	for(const MultiAtlasVersion& v : detail.family.versions) {
		ValueMap item; AddValue(item, "vid", v.vid); item.Add("code", v.code);
		ValueArray requires;
		for(const String& literal : v.requires) requires.Add(literal);
		item.Add("requires", requires);
		versions.Add(item);
	}
	root.Add("versions", versions);
	ValueArray actions;
	for(const MultiAtlasAction& a : detail.rules.actions) {
		ValueMap item; AddValue(item, "name", a.name); AddValue(item, "text", a.text);
		ValueArray requires, effects;
		for(const String& value : a.requires) requires.Add(value);
		for(const String& value : a.effects) effects.Add(value);
		item.Add("requires", requires); item.Add("effects", effects);
		actions.Add(item);
	}
	root.Add("actions", actions);
	ValueArray events;
	for(const MultiAtlasEvent& e : detail.events) {
		ValueMap item;
		AddValue(item, "id", e.id); AddValue(item, "vid", e.vid);
		AddValue(item, "subject_kind", e.subject_kind); AddValue(item, "subject_id", e.subject_id);
		AddValue(item, "type", e.type); AddValue(item, "action", e.action);
	AddValue(item, "text", e.text); AddValue(item, "at", e.at);
		ValueArray requires, effects;
		for(const String& value : e.requires) requires.Add(value);
		for(const String& value : e.effects) effects.Add(value);
		item.Add("requires", requires); item.Add("effects", effects);
		events.Add(item);
	}
	root.Add("events", events);
	return root;
}

static String MultiAtlasDetailEsc(const String& value)
{
	StringBuffer out;
	for(int i = 0; i < value.GetCount(); i++) switch(value[i]) {
	case '&': out << "&amp;"; break; case '<': out << "&lt;"; break;
	case '>': out << "&gt;"; break; case '"': out << "&quot;"; break;
	case '\'': out << "&#39;"; break; default: out.Cat(value[i]);
	}
	return String(out);
}

static String MultiAtlasDetailJson(const MultiAtlasTrackDetail& detail)
{
	String json = AsJSON(MultiAtlasDetailValue(detail));
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

} // namespace

String MultiAtlasClientHtml(const MultiAtlasTrackDetail& detail)
{
	StringBuffer out;
	out << "<section class=\"matd\" data-return=\"" << MultiAtlasDetailEsc(detail.overview_url)
	    << "\" data-track-route=\"" << MultiAtlasDetailEsc(detail.track_route_base) << "\">"
	    << "<nav class=\"matd-crumb\" aria-label=\"Track breadcrumb\"><a href=\""
	    << MultiAtlasDetailEsc(detail.overview_url) << "\">" << MultiAtlasDetailEsc(detail.ensemble_title)
	    << "</a><span aria-hidden=\"true\">›</span><strong>" << MultiAtlasDetailEsc(detail.track_id)
	    << "</strong><span aria-hidden=\"true\">›</span><span>" << MultiAtlasDetailEsc(detail.version)
	    << "</span><a class=\"matd-return\" href=\"" << MultiAtlasDetailEsc(detail.overview_url)
	    << "\">Return to Meta-Map</a></nav>"
	    << "<div class=\"matd-toolbar\" aria-label=\"Track diagram controls\">"
	    << "<button type=\"button\" data-act=\"up\" aria-label=\"Go to parent map\">Up</button>"
	    << "<output data-plane aria-live=\"polite\">Whole track</output>"
	    << "<label><input type=\"checkbox\" data-tiles> Tile overview</label>"
	    << "<button type=\"button\" data-act=\"in\" aria-label=\"Zoom in\">+</button>"
	    << "<button type=\"button\" data-act=\"out\" aria-label=\"Zoom out\">−</button>"
	    << "<button type=\"button\" data-act=\"fit\">Fit</button>"
	    << "<button type=\"button\" data-act=\"one\">1:1</button><output data-zoom>100%</output></div>"
	    << "<div class=\"matd-layout\"><div class=\"matd-stage\"><svg class=\"matd-canvas\" role=\"img\" aria-label=\""
	    << MultiAtlasDetailEsc(detail.track_id) << " track diagram\" tabindex=\"0\"></svg></div>"
	    << "<aside class=\"matd-inspector\" aria-live=\"polite\"><h2>Element details</h2><p data-empty>Select an element to inspect its entry, outcome and task status.</p><div data-detail hidden></div></aside></div>"
	    << "<script type=\"application/json\" data-track-data>" << MultiAtlasDetailJson(detail) << "</script></section>";
	return String(out);
}

String MultiAtlasClientStylesheet()
{
	return R"MATD(<style>
.matd { color:var(--fg); }
.matd-crumb,.matd-toolbar { display:flex; flex-wrap:wrap; gap:10px; align-items:center; padding:8px 0; }
.matd-crumb a { color:var(--accent); }
.matd-return { margin-left:auto; font-weight:600; }
.matd button,.matd input { color:var(--fg); background:var(--bg); border:1px solid var(--line); border-radius:5px; padding:5px 9px; font:inherit; }
.matd button { cursor:pointer; }
.matd-toolbar label { display:flex; gap:6px; align-items:center; }
.matd-toolbar output { color:var(--muted); font-variant-numeric:tabular-nums; }
.matd-layout { display:grid; grid-template-columns:minmax(0,1fr) minmax(240px,300px); gap:12px; align-items:stretch; }
.matd-stage { min-width:0; height:68vh; min-height:380px; border:1px solid var(--line); border-radius:8px; overflow:hidden; background:var(--panel); touch-action:none; }
.matd-canvas { display:block; width:100%; height:100%; cursor:grab; }
.matd-canvas:active { cursor:grabbing; }
.matd-canvas .plane-bg { fill:var(--panel); }
.matd-canvas .sea { fill:var(--sea); opacity:.55; stroke:var(--line); }
.matd-canvas .element { fill:var(--bg); stroke:var(--line); stroke-width:2; }
.matd-canvas .element.kind-block { stroke-width:3; }
.matd-canvas .element.kind-continent { stroke-width:4; }
.matd-canvas .element.status-hot { stroke:var(--st-hot); fill:color-mix(in srgb,var(--st-hot) 18%,var(--panel)); }
.matd-canvas .element.status-active { stroke:var(--st-active); }
.matd-canvas .element.status-planned { stroke:var(--st-planned); }
.matd-canvas .element.status-not-ready { stroke:var(--st-notready); stroke-dasharray:7 4; }
.matd-canvas .element.status-future { stroke:var(--st-future); stroke-dasharray:3 5; }
.matd-canvas .element.status-settled { stroke:var(--st-settled); fill:color-mix(in srgb,var(--st-settled) 14%,var(--panel)); }
.matd-canvas .element.status-trouble { stroke:var(--st-trouble); fill:color-mix(in srgb,var(--st-trouble) 18%,var(--panel)); }
.matd-canvas .label { fill:var(--fg); font:600 14px system-ui,sans-serif; pointer-events:none; }
.matd-canvas .badge { fill:var(--muted); font:12px system-ui,sans-serif; pointer-events:none; }
.matd-canvas .road { fill:none; stroke:var(--muted); stroke-width:2.2; }
.matd-canvas .road.kind-communication { stroke-dasharray:7 5; }
.matd-canvas .road.kind-dependency { stroke:var(--st-hot); stroke-dasharray:3 5; }
.matd-canvas .road.external { stroke:var(--accent); stroke-dasharray:8 5; pointer-events:none; }
.matd-canvas .road-hit { fill:none; stroke:transparent; stroke-width:18; pointer-events:stroke; cursor:pointer; }
.matd-canvas .road-label { fill:var(--fg); font:12px system-ui,sans-serif; pointer-events:none; }
.matd-canvas [data-id] { cursor:pointer; }
.matd-canvas [data-id].selected > .element { stroke:var(--accent); stroke-width:4; }
.matd-inspector { background:var(--panel); border:1px solid var(--line); border-radius:8px; padding:12px; overflow:auto; }
.matd-inspector h2 { font-size:1.05rem; margin:0 0 8px; overflow-wrap:anywhere; }
.matd-inspector p { color:var(--muted); }
.matd-inspector dl { display:grid; grid-template-columns:max-content minmax(0,1fr); gap:7px 12px; margin:0; }
.matd-inspector dt { color:var(--muted); }
.matd-inspector dd { margin:0; overflow-wrap:anywhere; white-space:pre-wrap; }
.matd-inspector [data-entry],.matd-inspector [data-outcome] { display:block; margin-top:12px; }
@media(max-width:760px) { .matd-layout { grid-template-columns:1fr; } .matd-stage { height:58vh; min-height:320px; } .matd-inspector { min-height:160px; } .matd-return { margin-left:0; width:100%; } }
@media(prefers-reduced-motion:reduce) { .matd-canvas * { scroll-behavior:auto; } }
</style>)MATD";
}

String MultiAtlasClientScript()
{
	return R"MATD(<script>
(function () {
  "use strict";
  var NS = "http://www.w3.org/2000/svg";
  document.querySelectorAll(".matd").forEach(function (shell) {
    var data = JSON.parse(shell.querySelector("[data-track-data]").textContent),
      map = data.map,
      svg = shell.querySelector(".matd-canvas");
    var stage = shell.querySelector(".matd-stage"),
      zoom = shell.querySelector("[data-zoom]"),
      planeLabel = shell.querySelector("[data-plane]");
    var inspector = shell.querySelector("[data-detail]"),
      empty = shell.querySelector("[data-empty]"),
      tiles = shell.querySelector("[data-tiles]");
    var view = {
      plane: "",
      scale: 1,
      x: 12,
      y: 12,
      manual: false,
      tiles: false,
      root: null,
      width: 1,
      height: 1,
    };
    var pointers = Object.create(null),
      drag = null,
      pinch = null,
      moved = false;
    function node(name, attrs, text) {
      var n = document.createElementNS(NS, name);
      Object.keys(attrs || {}).forEach(function (k) {
        n.setAttribute(k, attrs[k]);
      });
      if (text !== undefined) n.textContent = text;
      return n;
    }
    function add(parent, name, attrs, text) {
      var n = node(name, attrs, text);
      parent.appendChild(n);
      return n;
    }
    function find(id) {
      return (
        (map.elements || []).filter(function (e) {
          return e.id === id;
        })[0] || null
      );
    }
    function kids(id) {
      return (map.elements || []).filter(function (e) {
        return (e.parent || "") === id;
      });
    }
    function currentChildren() {
      return kids(view.plane);
    }
    function pathLabel(id) {
      var labels = [],
        e = find(id),
        guard = 0;
      while (e && guard++ <= map.elements.length) {
        labels.unshift(e.name || e.id);
        e = e.parent ? find(e.parent) : null;
      }
      return labels.join(" / ");
    }
    function transform() {
      if (view.root)
        view.root.setAttribute(
          "transform",
          "translate(" + view.x + " " + view.y + ") scale(" + view.scale + ")",
        );
      zoom.textContent = Math.round(view.scale * 100) + "%";
    }
    function fit() {
      var W = stage.clientWidth || 800,
        H = stage.clientHeight || 500;
      view.scale = Math.max(
        0.035,
        Math.min(2, Math.min((W - 36) / view.width, (H - 36) / view.height)),
      );
      view.x = (W - view.width * view.scale) / 2;
      view.y = (H - view.height * view.scale) / 2;
      view.manual = false;
      transform();
    }
    function zoomAt(x, y, f) {
      var n = Math.max(0.035, Math.min(6, view.scale * f)),
        k = n / view.scale;
      view.x = x - (x - view.x) * k;
      view.y = y - (y - view.y) * k;
      view.scale = n;
      view.manual = true;
      transform();
    }
    function tileLayout(children) {
      var cols = Math.max(1, Math.ceil(Math.sqrt(children.length))),
        w = 220,
        h = 100,
        g = 24;
      view.width = cols * w + (cols - 1) * g;
      view.height =
        Math.ceil(children.length / cols) * h +
        (Math.ceil(children.length / cols) - 1) * g;
      return children.map(function (e, i) {
        return {
          x: (i % cols) * (w + g),
          y: Math.floor(i / cols) * (h + g),
          w: w,
          h: h,
        };
      });
    }
    function renderTree(parent, e, offset, positions, ancestors) {
      var path = ancestors || Object.create(null);
      if (path[e.id]) return;
      path[e.id] = true;
      var r = {
        x: offset.x + (Number(e.x) || 0),
        y: offset.y + (Number(e.y) || 0),
        w: Math.max(40, Number(e.w) || 140),
        h: Math.max(32, Number(e.h) || 72),
      };
      positions[e.id] = r;
      var g = add(parent, "g", {
        "data-id": e.id,
        tabindex: "0",
        role: "button",
        "aria-label": (e.name || e.id) + ", " + (e.status || "unknown"),
      });
      add(g, "rect", {
        x: r.x,
        y: r.y,
        width: r.w,
        height: r.h,
        rx: e.kind === "place" ? 6 : 12,
        class:
          "element kind-" +
          (e.kind || "place") +
          " status-" +
          (e.status || "planned"),
      });
      add(
        g,
        "text",
        { x: r.x + 10, y: r.y + 22, class: "label" },
        e.name || e.id,
      );
      add(
        g,
        "text",
        { x: r.x + 10, y: r.y + 40, class: "badge" },
        (e.kind || "element") + " · " + (e.status || "planned"),
      );
      var children = kids(e.id);
      if (children.length)
        add(
          g,
          "text",
          { x: r.x + 10, y: r.y + r.h - 9, class: "badge" },
          children.length + " nested elements · Enter to open",
        );
      g.addEventListener("click", function (ev) {
        if (!moved) {
          select(e);
        }
      });
      g.addEventListener("dblclick", function () {
        if (children.length) enter(e.id);
      });
      g.addEventListener("keydown", function (ev) {
        if (ev.key === "Enter" || ev.key === " ") {
          ev.preventDefault();
          select(e);
        } else if (ev.key === "ArrowRight" && children.length) {
          ev.preventDefault();
          enter(e.id);
        }
      });
      children.forEach(function (child) {
        renderTree(g, child, { x: r.x, y: r.y }, positions, path);
      });
      delete path[e.id];
    }
    function draw() {
      while (svg.firstChild) svg.removeChild(svg.firstChild);
      var W = stage.clientWidth || 800,
        H = stage.clientHeight || 500;
      svg.setAttribute("viewBox", "0 0 " + W + " " + H);
      var children = currentChildren(),
        positions = Object.create(null),
        root = add(svg, "g", {});
      view.root = root;
      if (view.tiles) {
        var layout = tileLayout(children);
        add(root, "rect", {
          x: 0,
          y: 0,
          width: view.width,
          height: view.height,
          class: "plane-bg",
        });
        children.forEach(function (e, i) {
          var t = layout[i],
            g = add(root, "g", {
              "data-id": e.id,
              tabindex: "0",
              role: "button",
            });
          positions[e.id] = t;
          add(g, "rect", {
            x: t.x,
            y: t.y,
            width: t.w,
            height: t.h,
            rx: 10,
            class:
              "element kind-" +
              (e.kind || "place") +
              " status-" +
              (e.status || "planned"),
          });
          add(
            g,
            "text",
            { x: t.x + 12, y: t.y + 30, class: "label" },
            e.name || e.id,
          );
          add(
            g,
            "text",
            { x: t.x + 12, y: t.y + 55, class: "badge" },
            kids(e.id).length + " nested · " + (e.status || "planned"),
          );
          g.addEventListener("click", function () {
            select(e);
          });
          g.addEventListener("dblclick", function () {
            if (kids(e.id).length) enter(e.id);
          });
          g.addEventListener("keydown", function (ev) {
            if (ev.key === "Enter") {
              ev.preventDefault();
              if (kids(e.id).length) enter(e.id);
              else select(e);
            }
          });
        });
      } else {
        if (view.plane) {
          view.width = 1;
          view.height = 1;
          children.forEach(function (element) {
            view.width = Math.max(
              view.width,
              (Number(element.x) || 0) + (Number(element.w) || 120),
            );
            view.height = Math.max(
              view.height,
              (Number(element.y) || 0) + (Number(element.h) || 70),
            );
          });
        } else {
          view.width = Math.max(1, (map.world && map.world.width) || 800);
          view.height = Math.max(1, (map.world && map.world.height) || 600);
        }
        add(root, "rect", {
          x: 0,
          y: 0,
          width: view.width,
          height: view.height,
          class: "plane-bg",
        });
        children.forEach(function (e) {
          renderTree(root, e, { x: 0, y: 0 }, positions);
        });
      }
      (map.roads || []).forEach(function (road) {
        var fromTrack = road.from_track || data.track,
          toTrack = road.to_track || data.track;
        var localFrom = fromTrack === data.track;
        var a = positions[localFrom ? road.from : road.to];
        if (!a) return;
        var external = fromTrack !== toTrack,
          targetTrack = localFrom ? toTrack : fromTrack,
          targetId = localFrom ? road.to : road.from;
        var start = localFrom
            ? positions[road.from] || a
            : positions[road.to] || a,
          end;
        var sy = start.y + start.h / 2;
        if (external) {
          var outward = localFrom ? 1 : -1;
          var anchorId = localFrom ? road.from : road.to;
          var ports = (map.roads || [])
            .filter(function (candidate) {
              var candidateFrom = candidate.from_track || data.track;
              var candidateTo = candidate.to_track || data.track;
              var candidateLocalFrom = candidateFrom === data.track;
              var candidateAnchor = candidateLocalFrom
                ? candidate.from
                : candidate.to;
              return (
                candidateFrom !== candidateTo &&
                candidateLocalFrom === localFrom &&
                candidateAnchor === anchorId
              );
            })
            .sort(function (a, b) {
              var aKey =
                (a.from_track || data.track) + "\n" + a.to + "\n" + a.label;
              var bKey =
                (b.from_track || data.track) + "\n" + b.to + "\n" + b.label;
              return aKey.localeCompare(bKey);
            });
          var portIndex = ports.indexOf(road);
          var portOffset = (portIndex - (ports.length - 1) / 2) * 16;
          sy = Math.max(
            start.y + 8,
            Math.min(start.y + start.h - 8, sy + portOffset),
          );
          end = {
            x: outward > 0 ? view.width - 14 : 14,
            y: sy + (outward > 0 ? 30 : -30),
          };
        } else {
          end = positions[road.to];
          if (!end) return;
        }
        var sx = external
            ? localFrom
              ? start.x + start.w
              : start.x
            : start.x + start.w / 2,
          ex = external ? end.x : end.x + end.w / 2,
          ey = external ? end.y : end.y + end.h / 2;
        var bend = Math.max(35, Math.abs(ex - sx) * 0.35),
          direction = ex >= sx ? 1 : -1,
          d =
            "M" +
            sx +
            " " +
            sy +
            " C" +
            (sx + bend * direction) +
            " " +
            sy +
            " " +
            (ex - bend * direction) +
            " " +
            ey +
            " " +
            ex +
            " " +
            ey;
        var cls =
          "road " + (road.kind || "dependency") + (external ? " external" : "");
        if (external) {
          var hit = add(root, "path", {
            d: d,
            class: "road-hit",
            tabindex: "0",
            role: "link",
            "aria-label":
              (road.label || "External dependency") + " to " + targetTrack,
            "data-target-track": targetTrack,
            "data-target-element": targetId,
          });
          hit.addEventListener("keydown", function (ev) {
            if (ev.key === "Enter" || ev.key === " ") {
              ev.preventDefault();
              navigateExternal(hit);
            }
          });
        }
        add(root, "path", { d: d, class: cls });
        if (road.label)
          add(
            root,
            "text",
            { x: (sx + ex) / 2, y: (sy + ey) / 2 - 5, class: "road-label" },
            road.label + (external ? " → " + targetTrack : ""),
          );
        if (external && road.label)
          add(root, "title", {}, road.label + " → " + targetTrack);
      });
      planeLabel.textContent = view.plane
        ? pathLabel(view.plane)
        : "Whole track";
      transform();
    }
    function textList(values) {
      if (!values || !values.length) return "None recorded";
      var frag = document.createElement("span");
      values.forEach(function (v, i) {
        if (i) frag.appendChild(document.createTextNode(", "));
        frag.appendChild(document.createTextNode(v));
      });
      return frag;
    }
    function detailRow(dl, label, value) {
      var dt = document.createElement("dt"),
        dd = document.createElement("dd");
      dt.textContent = label;
      if (value && value.nodeType) dd.appendChild(value);
      else dd.textContent = value || "None recorded";
      dl.appendChild(dt);
      dl.appendChild(dd);
    }
    function select(e) {
      shell.querySelectorAll("[data-id].selected").forEach(function (n) {
        n.classList.remove("selected");
      });
      var nodeList = svg.querySelectorAll("[data-id]");
      for (var i = 0; i < nodeList.length; i++)
        if (nodeList[i].getAttribute("data-id") === e.id)
          nodeList[i].classList.add("selected");
      empty.hidden = true;
      inspector.hidden = false;
      while (inspector.firstChild) inspector.removeChild(inspector.firstChild);
      var h = document.createElement("h2");
      h.textContent = e.name || e.id;
      inspector.appendChild(h);
      var dl = document.createElement("dl");
      inspector.appendChild(dl);
      detailRow(dl, "ID", e.id);
      detailRow(dl, "Kind", e.kind);
      detailRow(dl, "Status", e.status);
      detailRow(dl, "Map path", pathLabel(e.id));
      detailRow(dl, "Goal", e.goal_picked ? "Picked" : "Not picked");
      if (e.goal_note) detailRow(dl, "Goal note", e.goal_note);
      if (e.notes) detailRow(dl, "Notes", e.notes);
      var related = (data.events || [])
        .filter(function (ev) {
          return (
            ev.subject_id === e.id &&
            (ev.subject_kind === "element" || ev.subject_kind === "task")
          );
        })
        .sort(function (a, b) {
          return (
            (a.at || "").localeCompare(b.at || "") ||
            (a.id || "").localeCompare(b.id || "")
          );
        });
      var latest = related.length ? related[related.length - 1] : null;
      var tasks = related.filter(function (ev) {
        return ev.subject_kind === "task";
      });
      var latestTask = tasks.length ? tasks[tasks.length - 1] : null;
      var taskEvent = latestTask || latest;
      var version = taskEvent
        ? (data.versions || []).filter(function (v) {
            return v.vid === taskEvent.vid;
          })[0]
        : null;
      var entries = ((version && version.requires) || []).slice();
      ((taskEvent && taskEvent.requires) || []).forEach(function (value) {
        if (entries.indexOf(value) < 0) entries.push(value);
      });
      detailRow(dl, "Entry requirements", textList(entries));
      var action = taskEvent
        ? (data.actions || []).filter(function (a) {
            return a.name === taskEvent.action;
          })[0]
        : null;
      detailRow(
        dl,
        "Task",
        taskEvent
          ? taskEvent.text || taskEvent.action || taskEvent.type || "Event"
          : "No task event recorded",
      );
      detailRow(
        dl,
        "Task status",
        latestTask
          ? latestTask.type
          : latest
            ? latest.type
            : "No task status event recorded",
      );
      var outcome =
        taskEvent && taskEvent.effects && taskEvent.effects.length
          ? taskEvent.effects
          : (action && action.effects) || [];
      detailRow(dl, "Outcome", textList(outcome));
      if (latest)
        detailRow(
          dl,
          "Last event",
          latest.type + " · " + latest.vid + " · " + latest.at,
        );
    }
    function enter(id) {
      view.plane = id;
      view.manual = false;
      draw();
      fit();
    }
    shell.querySelector("[data-act=up]").addEventListener("click", function () {
      var parent = view.plane ? find(view.plane) : null;
      enter(parent && parent.parent ? parent.parent : "");
    });
    shell.querySelectorAll("[data-act]").forEach(function (b) {
      b.addEventListener("click", function () {
        var a = b.dataset.act;
        if (a === "in")
          zoomAt(stage.clientWidth / 2, stage.clientHeight / 2, 1.25);
        else if (a === "out")
          zoomAt(stage.clientWidth / 2, stage.clientHeight / 2, 1 / 1.25);
        else if (a === "fit") fit();
        else if (a === "one") {
          view.scale = 1;
          view.x = 0;
          view.y = 0;
          view.manual = true;
          transform();
        }
      });
    });
    tiles.addEventListener("change", function () {
      view.tiles = tiles.checked;
      draw();
      fit();
    });
    svg.addEventListener(
      "wheel",
      function (ev) {
        ev.preventDefault();
        var r = svg.getBoundingClientRect();
        zoomAt(
          ev.clientX - r.left,
          ev.clientY - r.top,
          ev.deltaY < 0 ? 1.15 : 1 / 1.15,
        );
      },
      { passive: false },
    );
    svg.addEventListener("pointerdown", function (ev) {
      pointers[ev.pointerId] = { x: ev.clientX, y: ev.clientY };
      svg.setPointerCapture(ev.pointerId);
      moved = false;
      var keys = Object.keys(pointers);
      if (keys.length === 2) {
        var a = pointers[keys[0]],
          b = pointers[keys[1]];
        pinch = {
          distance: Math.hypot(a.x - b.x, a.y - b.y),
          scale: view.scale,
          midX: (a.x + b.x) / 2,
          midY: (a.y + b.y) / 2,
          tx: view.x,
          ty: view.y,
        };
        drag = null;
      } else {
        drag = {
          id: ev.pointerId,
          x: ev.clientX,
          y: ev.clientY,
          tx: view.x,
          ty: view.y,
          target: ev.target.closest("[data-id]"),
          road: ev.target.closest("[data-target-track]"),
        };
      }
    });
    svg.addEventListener("pointermove", function (ev) {
      if (!pointers[ev.pointerId]) return;
      pointers[ev.pointerId] = { x: ev.clientX, y: ev.clientY };
      var keys = Object.keys(pointers);
      if (pinch && keys.length >= 2) {
        var a = pointers[keys[0]],
          b = pointers[keys[1]],
          distance = Math.hypot(a.x - b.x, a.y - b.y),
          midX = (a.x + b.x) / 2,
          midY = (a.y + b.y) / 2,
          r = svg.getBoundingClientRect(),
          scale = Math.max(
            0.035,
            Math.min(6, (pinch.scale * distance) / Math.max(1, pinch.distance)),
          ),
          k = scale / pinch.scale;
        view.x = midX - r.left - (pinch.midX - r.left - pinch.tx) * k;
        view.y = midY - r.top - (pinch.midY - r.top - pinch.ty) * k;
        view.scale = scale;
        view.manual = true;
        moved = true;
        transform();
        return;
      }
      if (!drag || drag.id !== ev.pointerId) return;
      var dx = ev.clientX - drag.x,
        dy = ev.clientY - drag.y;
      if (Math.abs(dx) + Math.abs(dy) > 5) {
        moved = true;
        view.x = drag.tx + dx;
        view.y = drag.ty + dy;
        view.manual = true;
        transform();
      }
    });
    function navigateExternal(hit) {
      var targetTrack = hit.getAttribute("data-target-track");
      var targetElement = hit.getAttribute("data-target-element");
      var targetRoute =
        targetTrack === "meta"
          ? shell.dataset.return
          : shell.dataset.trackRoute + "/" + encodeURIComponent(targetTrack);
      var fragment =
        targetTrack === "meta"
          ? "#track=meta&element=" + encodeURIComponent(targetElement)
          : "#element=" + encodeURIComponent(targetElement);
      window.location.href = targetRoute + fragment;
    }
    function pointerEnd(ev) {
      delete pointers[ev.pointerId];
      if (Object.keys(pointers).length < 2) pinch = null;
      if (drag && drag.id === ev.pointerId) {
        if (!moved && drag.road) navigateExternal(drag.road);
        else if (!moved && drag.target) {
          var id = drag.target.getAttribute("data-id"),
            e = find(id);
          if (e) select(e);
        }
        drag = null;
      }
      if (Object.keys(pointers).length === 0) {
        window.setTimeout(function () {
          moved = false;
        }, 0);
      }
    }
    svg.addEventListener("pointerup", pointerEnd);
    svg.addEventListener("pointercancel", pointerEnd);
    svg.addEventListener("keydown", function (ev) {
      if (ev.key === "0") fit();
      else if (ev.key === "+" || ev.key === "=")
        zoomAt(stage.clientWidth / 2, stage.clientHeight / 2, 1.2);
      else if (ev.key === "-")
        zoomAt(stage.clientWidth / 2, stage.clientHeight / 2, 1 / 1.2);
      else if (ev.key === "ArrowLeft") {
        view.x += 30;
        transform();
      } else if (ev.key === "ArrowRight") {
        view.x -= 30;
        transform();
      } else if (ev.key === "ArrowUp") {
        view.y += 30;
        transform();
      } else if (ev.key === "ArrowDown") {
        view.y -= 30;
        transform();
      }
    });
    window.addEventListener("resize", function () {
      if (!view.manual) fit();
    });
    draw();
    fit();
    var focus = /element=([^&]+)/.exec(window.location.hash);
    if (focus) {
      try {
        var target = find(decodeURIComponent(focus[1]));
        if (target) {
          if (target.parent) view.plane = target.parent;
          draw();
          fit();
          select(target);
        }
      } catch (ignore) {
        /* malformed fragment is ignored */
      }
    }
  });
})();
</script>)MATD";
}

END_UPP_NAMESPACE
