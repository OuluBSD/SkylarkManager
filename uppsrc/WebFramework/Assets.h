#ifndef _WebFramework_Assets_h_
#define _WebFramework_Assets_h_

// Returns the text of a built-in WebFramework asset (assets/<name>), read from
// static/WebFramework/<name> next to the executable. See deploy-assets.sh.

String WebFrameworkAsset(const char *name);

#endif
