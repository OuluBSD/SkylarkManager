#ifndef _WebFramework_Icons_h_
#define _WebFramework_Icons_h_

// Vivid SVG vector icon registry

struct VividIcon {
	static String Get(const String& name, int size = 18, const String& color = "currentColor");
	static Vector<String> ListNames();
};

#endif
