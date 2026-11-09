#include "WebFramework.h"

NAMESPACE_UPP

// Assets are read once and cached. A missing asset throws rather than
// rendering an unstyled page: a page without its CSS or JS is a defect.
String WebFrameworkAsset(const char *name)
{
	static Mutex lock;
	static VectorMap<String, String> cache;
	Mutex::Lock __(lock);
	int i = cache.Find(name);
	if(i >= 0)
		return cache[i];
	String path = GetExeDirFile(AppendFileName("static/WebFramework", name));
	if(!FileExists(path))
		throw Exc("WebFramework asset missing: " + path + " (run uppsrc/WebFramework/deploy-assets.sh)");
	String text = LoadFile(path);
	cache.Add(name, text);
	return text;
}

END_UPP_NAMESPACE
