#ifndef _Atlas_AtlasDiagram_h_
#define _Atlas_AtlasDiagram_h_

#include "AtlasModel.h"

NAMESPACE_UPP

// The version diagram: one row per version, top to bottom in version order. Pure data, no drawing; the roadmap
// page lays it out. Bubbles are of four kinds:
//   work         a planned unit: a stage event with an action, run in that version
//   gate         a contract literal that is not a feature (for example artifact:...)
//   accomplished a contract literal done:<element>: a feature the version declares
//   banner       heads each minor group (0.k.*) and names what the group delivers, with its declared classes and functions
//   declaration  the files and signatures (names and types, no bodies) that a work unit declares, under the work
//   task         a planned task a person or worker must finish; its literal task:<id>:done is what work waits for
//   taskgroup    the tasks logged in one version, as one block, when there are more than a few
//   interface    the user actions (cli, gui, api, overlay) an element exposes, under its work bubble
// Arrows join a producer to what it feeds: an effect to the contract literal or entry it satisfies.

struct AtlasDiagramBubble : Moveable<AtlasDiagramBubble> {
	String         kind;          // work | gate | accomplished | banner | declaration | task | taskgroup | interface
	int            row = -1;
	String         text;          // a work bubble: the element name; others: the literal or the banner text
	String         literal;       // gate and accomplished: the contract literal; empty otherwise
	String         element;       // work: the element id; accomplished: the element id when known; task: the element it links to
	String         pkg;           // work: the U++ package the element implements, or a virtual group for data
	String         action;        // work: the action name
	String         detail;        // work: the action's text
	Vector<String> requires;      // work: entry literals
	Vector<String> effects;       // work: outcome literals
	Vector<String> waits;         // work: task:<id>:done entry literals, shown as waiting, not as unmet
	Vector<String> declares;      // declaration: the file path, then one signature line per class or function; taskgroup: one line per task; interface: one line per user action
	Vector<String> literals;      // taskgroup: the task:<id>:done literal of each task in the group
	String         assignee;      // task: who is responsible, when the log names one
	bool           processing = false; // work: a machine run (dataset processing, training, evaluation), not coding work
	bool           unmet = false; // work: an entry literal no earlier contract or work produces
};

struct AtlasDiagramArrow : Moveable<AtlasDiagramArrow> {
	int from = -1;
	int to = -1;
};

struct AtlasDiagramRow : Moveable<AtlasDiagramRow> {
	String         vid;
	String         label;
	bool           group_start = false;
	bool           beta = false;  // a x.999 row: a beta of the next minor group, never a release
	Vector<int>    bubbles;
};

struct AtlasVersionDiagram : Moveable<AtlasVersionDiagram> {
	Vector<AtlasDiagramRow>    rows;
	Vector<AtlasDiagramBubble> bubbles;
	Vector<AtlasDiagramArrow>  arrows;
};

// Builds the diagram for the family's versions. files are the family's file records, whose class and function spans
// are the declarations. hidden lists element ids the viewer may not see: their work, declaration and accomplished
// bubbles are left out, so are arrows to or from them, and they add nothing to a banner's counts.
void AtlasBuildDiagram(const AtlasFamily& f, const AtlasMap& head, const AtlasRules& rules,
                       const Vector<AtlasEvent>& events, const Vector<AtlasFile>& files, const Vector<String>& hidden,
                       AtlasVersionDiagram& out);

// The minor group of a version label: "0.2.5" -> "0.2".
String AtlasMinorGroup(const String& label);

END_UPP_NAMESPACE

#endif
