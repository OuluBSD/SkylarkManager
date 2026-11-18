#include "MultiAtlasModel.h"

NAMESPACE_UPP

int MultiAtlasMap::FindElement(const String& id) const
{
	for(int i = 0; i < elements.GetCount(); i++)
		if(elements[i].id == id)
			return i;
	return -1;
}

int MultiAtlasDepth(const MultiAtlasMap& map, const String& id)
{
	int depth = 0;
	String current = id;
	for(int guard = 0; guard <= map.elements.GetCount(); guard++) {
		int i = map.FindElement(current);
		if(i < 0)
			return -1;
		const String& parent = map.elements[i].parent;
		if(parent.IsEmpty())
			return depth;
		current = parent;
		depth++;
	}
	return -1;
}

Vector<String> MultiAtlasPath(const MultiAtlasMap& map, const String& id)
{
	Vector<String> path;
	String current = id;
	for(int guard = 0; guard <= map.elements.GetCount(); guard++) {
		int i = map.FindElement(current);
		if(i < 0) {
			path.Clear();
			return path;
		}
		path.Insert(0, current);
		if(map.elements[i].parent.IsEmpty())
			return path;
		current = map.elements[i].parent;
	}
	path.Clear();
	return path;
}

Vector<String> MultiAtlasPlaneElements(const MultiAtlasMap& map, const String& parent)
{
	Vector<String> result;
	for(const MultiAtlasElement& element : map.elements)
		if(element.parent == parent)
			result.Add(element.id);
	return result;
}

static bool Overlap(const MultiAtlasElement& a, const MultiAtlasElement& b)
{
	return (int64)a.x < (int64)b.x + b.w && (int64)b.x < (int64)a.x + a.w &&
	       (int64)a.y < (int64)b.y + b.h && (int64)b.y < (int64)a.y + a.h;
}

// 0 = sound chain, 1 = missing ancestor, 2 = parent cycle.
static int HierarchyProblem(const MultiAtlasMap& map, const String& id)
{
	Index<String> seen;
	String current = id;
	for(int guard = 0; guard <= map.elements.GetCount(); guard++) {
		int i = map.FindElement(current);
		if(i < 0)
			return 1;
		if(seen.Find(current) >= 0)
			return 2;
		seen.Add(current);
		current = map.elements[i].parent;
		if(current.IsEmpty())
			return 0;
	}
	return 2;
}

void MultiAtlasValidate(const MultiAtlasMap& map, MultiAtlasCheck& out)
{
	if(map.world.width <= 0 || map.world.height <= 0)
		out.errors.Add("world size must be positive");

	Index<String> ids;
	for(const MultiAtlasElement& e : map.elements) {
		if(e.id.IsEmpty())
			out.errors.Add("element id is required");
		else if(ids.Find(e.id) >= 0)
			out.errors.Add("duplicate element id: " + e.id);
		else
			ids.Add(e.id);
		if(e.w <= 0 || e.h <= 0)
			out.errors.Add("element " + e.id + " must have a positive size");
		if(e.x < 0 || e.y < 0)
			out.errors.Add("element " + e.id + " has a negative position");
	}

	for(const MultiAtlasElement& e : map.elements) {
		if(e.parent.IsEmpty()) {
			if(map.world.width > 0 && map.world.height > 0 &&
			   ((int64)e.x + e.w > map.world.width || (int64)e.y + e.h > map.world.height))
				out.errors.Add("element " + e.id + " is outside the world");
		}
		else {
			int p = map.FindElement(e.parent);
			if(p < 0) {
				out.errors.Add("element " + e.id + " has a missing parent: " + e.parent);
				continue;
			}
			int hierarchy_problem = HierarchyProblem(map, e.id);
			if(hierarchy_problem == 2) {
				out.errors.Add("element " + e.id + " is in a containment cycle");
				continue;
			}
			if(hierarchy_problem == 1) {
				out.errors.Add("element " + e.id + " has a missing ancestor");
				continue;
			}
			const MultiAtlasElement& parent = map.elements[p];
			if(e.w > 0 && e.h > 0 && parent.w > 0 && parent.h > 0 &&
			   ((int64)e.x + e.w > parent.w || (int64)e.y + e.h > parent.h))
				out.errors.Add("element " + e.id + " is outside parent " + parent.id);
		}
	}

	for(int i = 0; i < map.elements.GetCount(); i++) {
		const MultiAtlasElement& a = map.elements[i];
		if(a.w <= 0 || a.h <= 0)
			continue;
		for(int j = i + 1; j < map.elements.GetCount(); j++) {
			const MultiAtlasElement& b = map.elements[j];
			if(a.parent == b.parent && b.w > 0 && b.h > 0 && Overlap(a, b))
				out.errors.Add("sibling elements overlap: " + a.id + " and " + b.id);
		}
	}
}

END_UPP_NAMESPACE
