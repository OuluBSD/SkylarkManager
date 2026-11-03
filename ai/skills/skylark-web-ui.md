# Skylark Web UI Development

## Purpose

This skill enables creation of small-user-base web browser user interfaces using U++ Skylark framework. Skylark is a web development framework where a C++ console application serves HTML pages generated from `.witz` template files (HTML with Skylark template extensions).

**Reference packages**: `reference/Skylark01` through `reference/Skylark12`, `reference/SkylarkUpload`
**Framework package**: `uppsrc/Skylark`
**Template documentation**: `uppsrc/Skylark/src.tpp/Witz_en-us.tpp`

## When to Use Skylark

- Small user base web interfaces (internal tools, admin panels, device UIs)
- C++ application that needs a browser-based UI without external web server
- Rapid prototyping of web UIs with direct C++ backend logic
- Database-backed CRUD applications with minimal boilerplate

**Not suitable for**: High-traffic public websites, complex single-page applications (use separate frontend framework instead).

## Minimal Application Structure

```cpp
#include <Skylark/Skylark.h>

using namespace Upp;

SKYLARK(HomePage, "")
{
    http.RenderResult("MyApp/index");
}

struct MyApp : SkylarkApp {
    MyApp() {
        root = "myapp";
    #ifdef _DEBUG
        prefork = 0;
        use_caching = false;
    #endif
    }
};

CONSOLE_APP_MAIN
{
#ifdef _DEBUG
    StdLogSetup(LOG_FILE|LOG_COUT);
    Ini::skylark_log = true;
#endif
    MyApp().Run();
}
```

`.upp` file requirements:
```
uses
    Core,
    Skylark;

file
    main.cpp,
    index.witz;

mainconfig
    "" = "MT";
```

**Important**: `MT` flag is required in mainconfig. The app runs at `http://localhost:8001/myapp` by default.

## SKYLARK Handlers and Path Patterns

### Handler Definition

```cpp
SKYLARK(HandlerName, "path/pattern")
{
    // http object provides request/response access
    http << "<html>direct output</html>";
    // or render a template:
    http.RenderResult("PackageName/template");
}
```

### Path Patterns

| Pattern | Matches | Access |
|---------|---------|--------|
| `""` | Root only | - |
| `"page2"` | `/page2` | - |
| `"*/param"` | `/anything/param` | `http[0]` = wildcard value |
| `"params/**"` | `/params/a/b/c` | `http[0]`, `http[1]`, `http[2]`... |
| `"**"` | Everything (catch-all) | `http[i]` |
| `"submit:POST"` | POST-only handler | - |
| `"path:GET"` | GET-only handler | - |

### Common Response Operations

```cpp
// Direct HTML output
http << "<html><body>Hello</body></html>";

// Render witz template
http.RenderResult("PackageName/template");  // -> PackageName/template.witz

// Redirect to another handler
http.Redirect(HomePage);

// Serve file with content type
http.ContentType("image/png") << LoadFile(GetDataFile("favicon.png"));

// Plain content with explicit response
http.Content("text/plain", "OK");
http.Response(200, "OK");
```

### Accessing Parameters

```cpp
SKYLARK(Param, "*/param")
{
    http << "Parameter: " << http[0];     // path wildcard
}

SKYLARK(Form, "form")
{
    String id = http["id"];               // GET/POST form field
    int num = http.Int("number");         // as integer (0 if missing)
}
```

## Witz Template Language

Witz files are HTML with `$`-prefixed template code. `$$` produces a literal `$`.

### Variables

```witz
$MyValue                    <!-- HTML-escaped value -->
$raw(MyRawValue)            <!-- Unescaped raw HTML -->
$.SessionVar                <!-- Session variable (. prefix) -->
$@CookieVar                 <!-- Cookie variable (@ prefix) -->
```

Handler sets variables:
```cpp
http("MyValue", "some value")
    ("MyArray", valueArray)
    ("MyMap", valueMap)
    .RenderResult("MyApp/index");
```

### Expressions

Witz supports full expressions: arithmetic (`+ - * / % << >>`), comparison (`< > <= >= == !=`), logical (`&& || !`), ternary (`?:`), map/array indexing (`map[key]`, `map.field`, `array[i]`), string concatenation (`+`).

Literals follow JavaScript syntax: `123`, `-1.5`, `"string"`, `[1, "hello", 123]`, `{"key": "value"}`.

### Statements

```witz
$if(condition)
    shown when true
$else
    shown when false
$endif

$for(i in MyArray)
    $i._index. $i<br>
$endfor

$for(i in MyMap)
    $i._key: $i<br>
$/
```

Notes:
- `$/` is shorthand for `$endfor` / `$endif`
- `$else` in `$for` executes when container is empty
- Pseudo-fields: `._first`, `._last`, `._index` (0-based), `._key` (maps)

### Handler Links

Handler names become URL-generating functions in templates:

```witz
<a href=$HomePage>Home</a>              <!-- link to handler -->
<a href=$Param("test")>Param</a>        <!-- link with path argument -->
<a href=$Edit(i.ID)>Edit</a>            <!-- link with loop variable -->
```

### Template Inheritance (Preprocessing)

**base.witz** (layout):
```witz
<!DOCTYPE html>
<html>
<title>#TITLE</title>
<body>
#BODY
</body>
</html>

#define TITLE Generic title
#define BODY
```

**index.witz** (page):
```witz
#include Skylark04/base

#define TITLE MyApp title

#define BODY
This is the page body!
```

Rules:
- `#include path` inserts another template (language-specific variant tried first)
- `#define id` starts a subblock (ends at next `#define`)
- `#id` inserts the subblock; last definition wins
- Main block (before first `#define`) is the template output
- `##` produces literal `#`

### Standard Library Functions

| Function | Description |
|----------|-------------|
| `cycle(i, p1, ..., pn)` | Returns p[i mod n] (e.g. alternating row colors) |
| `raw(text)` | Output without HTML escaping |
| `count(array_or_map)` | Element count |
| `post_identity()` | Hidden CSRF field for forms (required for POST!) |
| `js_identity()` | Script for CSRF protection in AJAX |
| `set()` | Debug: HTML list of all shared variables |
| `render(template, p1, ...)` | Include template with parameters (as `$_1`, `$_2`...) |

## Forms and POST Handling

### Basic GET Form (Skylark05)

```witz
<form action=$Submit method="get" accept-charset="utf-8">
    <INPUT type="text" name="id">
    <INPUT type="submit" value="Submit">
</form>
```

```cpp
SKYLARK(Submit, "submit")
{
    http("RESULT", ToUpper((String)http["id"]))
        .RenderResult("MyApp/submit");
}
```

### POST Form with CSRF Protection (Skylark06)

```witz
<form action=$Submit method="post" accept-charset="utf-8" enctype="multipart/form-data">
    $post_identity()                       <!-- REQUIRED for POST -->
    <INPUT type="text" name="id">
    <INPUT type="submit" value="Submit">
</form>
```

```cpp
SKYLARK(Submit, "submit:POST")    // note :POST suffix
{
    http("RESULT", ToUpper((String)http["id"]))
        .RenderResult("MyApp/submit");
}
```

**CRITICAL**: Always include `$post_identity()` in POST forms. Skylark rejects POSTs without valid CSRF identity.

## Session Variables (Skylark07)

```cpp
SKYLARK(Submit, "submit:POST")
{
    ValueArray va;
    Value h = http[".LIST"];          // read session var (. prefix)
    if(IsValueArray(h))
        va = h;
    va.Add(http["id"]);
    http.SessionSet(".LIST", va);     // store session var
    http.Redirect(HomePage);
}
```

In template:
```witz
List ($count(.LIST)):<br>
$for(i in .LIST)
    "$i"
$endfor
```

## AJAX with UxPost (Skylark08)

```witz
<script type="text/javascript" src="$static/Skylark/skylark.js"></script>
$js_identity()
<div id="numbers">
    <INPUT type="text" id="number1">
    <INPUT type="text" id="number2">
    <INPUT type="button" value="Add" onclick='UxPost($Add, "numbers")'>
</div>
<div id="result"/>
```

```cpp
SKYLARK(Add, "add:POST")
{
    String r = AsString(Nvl(http.Int("number1")) + Nvl(http.Int("number2")));
    http.Ux("result", "The result is: " + r)   // set innerHTML of #result
        .UxSet("number1", r);                  // set value of #number1
}
```

Key client-side function: `UxPost($Handler, "divId")` posts the named div's input fields to handler. Server responds with `Ux*` calls manipulating the page.

## Database Integration (Skylark09, Skylark10)

### Schema File (myapp.sch)

```
TABLE_(PERSON)
    INT_    (ID)      PRIMARY_KEY AUTO_INCREMENT
    STRING_ (NAME, 200)
    STRING_ (LASTNAME, 200)
END_TABLE
```

### Setup (main.cpp)

```cpp
#include <Skylark/Skylark.h>
#include <plugin/sqlite3/Sqlite3.h>

#define  MODEL <MyApp/myapp.sch>
#define  SCHEMADIALECT <plugin/sqlite3/Sqlite3Schema.h>
#include <Sql/sch_header.h>
#include <Sql/sch_schema.h>
#include <Sql/sch_source.h>
```

### Query and Render (Skylark09)

```cpp
SKYLARK(HomePage, "")
{
    Sql sql;
    sql * Select(ID, NAME, LASTNAME).From(PERSON).OrderBy(LASTNAME, NAME);
    ValueArray person;
    ValueMap vm;
    while(sql.Fetch(vm))
        person.Add(vm);
    http("PERSON", person).RenderResult("MyApp/index");
}
```

Template iterates rows with field access:
```witz
$for(i in PERSON)
    <tr><td>$i._index.</td><td>$i.NAME</td><td>$i.LASTNAME</td></tr>
$/
```

### Insert/Update from Forms (Skylark10)

```cpp
SKYLARK(SubmitNew, "create/submit:POST")
{
    SQL * http.Insert(PERSON);              // auto-map form fields to columns
    http.Redirect(HomePage);
}

SKYLARK(SubmitEdit, "edit/submit/*:POST")
{
    SQL * http.Update(PERSON).Where(ID == http.Int(0));
    http.Redirect(HomePage);
}

SKYLARK(Edit, "edit/*")
{
    int id = http.Int(0);
    http(Select(SqlAll()).From(PERSON).Where(ID == id))  // row -> template vars
        ("ID", id)
        ("ACTION", SubmitEdit, id)                       // param'd handler
    .RenderResult("MyApp/Dialog");
}
```

Form field names must match lowercase column names (`name`, `lastname` for NAME, LASTNAME).

### Threading Note for SQLite3

```cpp
struct MyApp : SkylarkApp {
    virtual void WorkThread();
    MyApp() {
        threads = 1;  // Sqlite3 does not like threads
    }
};

void MyApp::WorkThread()
{
    Sqlite3Session sqlite3;
    if(!sqlite3.Open(ConfigFile("db"))) Exit(1);
    SQL = sqlite3;
    RunThread();
}
```

## Localization (Skylark11)

Language-specific templates: `index.witz` (default), `index.cs-cz.witz` (Czech).

```cpp
SKYLARK(SetLanguage, "setlanguage/*")
{
    int lang = LNGFromText(http[0]);
    if(lang)
        http.SetLanguage(lang);
    http.Redirect(HomePage);
}
```

```witz
<a href=$SetLanguage("en-us")>EN</a>
<a href=$SetLanguage("cs-cz")>CZ</a>
Current language: $.language
Variable value: $VAR        <!-- C++ side: t_("Aborted by user.") -->
```

## Reusable SkylarkPack Handlers (Skylark12)

Package Create-Edit-Delete handler sets as reusable components:

```cpp
struct CreateEditDelete : SkylarkPack {
    HandlerId back;
    SqlId     table;
    SqlId     key;
    SqlSet    columns;
    String    dialog;

    void Create(Http& http);
    void SubmitCreate(Http& http);
    void Edit(Http& http);
    void SubmitEdit(Http& http);
    void Delete(Http& http);
    // ... Use() registers all sub-handlers
};
```

Usage:
```cpp
SKYLARK_USE(CreateEditDelete, Person, "person")
{
    Person.back = HomePage;
    Person.table = PERSON;
    Person.dialog = "Skylark12/dialog";
}
```

Template links use `Pack:Handler` syntax:
```witz
<a href=$Person:Edit(i.ID)>Edit</a>
<a href=$Person:Delete(i.ID)>Delete</a>
<a href=$Person:Create>Insert new person</a>
```

Or store link in variable: `("CREATE", LINK(Person, Create))` → `$CREATE`.

## File Upload (SkylarkUpload)

```cpp
SKYLARK_PROGRESS(PostUpload, "upload:POST", &ProgressHandler)
{
    Value const &contents = http["filestoupload[]"];
    Value const &filenames = http["filestoupload.filename[]"];
    if(contents.IsNull() || filenames.IsNull())
        return;
    for(int i = 0; i < contents.GetCount(); i++)
        SaveFile(AppendFileName(Upload().GetRootPath(),
                 (String)filenames[i]), contents[i]);
}
```

Progress handler receives `PROGRESS_HEADER`, `PROGRESS_CONTENT`, `PROGRESS_END`, `PROGRESS_QUERY` events; store progress in session variables and poll from a `SKYLARK(Progress, "progress")` handler. Must be wrapped in `INTERLOCKED` for thread safety. Requires session: call `http.NewSessionId()` if session cookie is null.

## Static Files

`.css`, `.html`, `.js` files listed in `.upp` are automatically copied to `static/$(PACKAGE)/$(RELPATH)` and served via:

```witz
<link rel="stylesheet" href="$static/MyApp/style.css">
<script src="$static/Skylark/skylark.js"></script>
```

## Debugging Configuration

Standard debug block in every Skylark app:

```cpp
struct MyApp : SkylarkApp {
    MyApp() {
        root = "myapp";
    #ifdef _DEBUG
        prefork = 0;          // no process forking (easier debugging)
        use_caching = false;  // recompile witz templates on each request
    #endif
    }
};

CONSOLE_APP_MAIN
{
#ifdef _DEBUG
    StdLogSetup(LOG_FILE|LOG_COUT);
    Ini::skylark_log = true;
#endif
    MyApp().Run();
}
```

**Critical**: `use_caching = false` during development, otherwise witz changes won't show.

## Reference Package Map

| Package | Demonstrates |
|---------|--------------|
| Skylark01 | Handlers, path patterns (`*`, `**`), redirect, static content |
| Skylark02 | Template variables, `$raw()`, ValueArray/ValueMap, `$for` loops |
| Skylark03 | Multiple templates, handler links `$Handler(args)` |
| Skylark04 | Template inheritance: `#include`, `#define` (base layout pattern) |
| Skylark05 | GET forms, `http["field"]` |
| Skylark06 | POST forms, `$post_identity()` CSRF |
| Skylark07 | Session variables, `SessionSet`, `.VAR` in templates |
| Skylark08 | AJAX: `UxPost`, `$js_identity()`, `http.Ux`, `UxSet` |
| Skylark09 | SQL read: `.sch` schema, `Select`, fetch to ValueArray |
| Skylark10 | SQL CRUD: `http.Insert`, `http.Update`, parameterized handlers |
| Skylark11 | Localization: language witz variants, `SetLanguage`, `t_()` |
| Skylark12 | `SkylarkPack` reusable CRUD handler sets |
| SkylarkUpload | File upload with progress tracking (`SKYLARK_PROGRESS`) |

## Build and Run

```powershell
# Build a Skylark reference example
bin\build.exe -M 2 -m MSVS26x64 Skylark01

# Run (console app, serves HTTP)
bin\Skylark01
# Opens at http://localhost:8001/myapp
```

## Common Pitfalls

1. **Missing `MT` flag** in mainconfig - Skylark requires multithreading
2. **Forgot `$post_identity()`** - POST forms fail CSRF check silently
3. **Template caching** - set `use_caching = false` in debug builds
4. **SQLite + threads** - set `threads = 1` and open session per WorkThread
5. **Form field case** - `http.Insert(TABLE)` maps lowercase field names to UPPERCASE columns
6. **Catch-all ordering** - `"**"` handler must be defined last (handlers match in registration order)
7. **HTML escaping** - values are escaped by default; use `$raw()` or `Raw()` only for trusted content

## Integration with Other Skills

- **gui-low-complexity / gui-medium-complexity** - Choose Skylark when users access via browser instead of desktop GUI
- **toon-file-format** - Export structured data from handlers in TOON format for AI consumption
- **upp-format-usage** - Format values in handlers before passing to templates
- **build-upp-package** - Building Skylark packages (remember MT flag)

## User Queries (Before Implementation)

```markdown
## Skylark Web App Configuration

1. **User base size?**
   - Small internal tool (< 100 concurrent users) → Skylark fits
   - Public/high-traffic → Consider other architecture

2. **Data model?**
   - Database needed? Which dialect (SQLite3 simplest)?
   - CRUD operations → consider SkylarkPack pattern (Skylark12)

3. **Interactivity level?**
   - Static pages → basic templates
   - Form-based → POST + post_identity
   - Dynamic updates → AJAX UxPost (Skylark08)

4. **Features needed?**
   - Sessions/login state?
   - File uploads?
   - Multiple languages?
   - Shared layout across pages (template inheritance)?

5. **Template organization?**
   - Single page or multi-page?
   - Base layout with #define sections?
```

## Checklist

- [ ] `MT` flag in mainconfig
- [ ] `uses Skylark` in .upp file
- [ ] SkylarkApp subclass with root set
- [ ] Debug config: prefork=0, use_caching=false
- [ ] All POST forms include `$post_identity()`
- [ ] POST handlers have `:POST` suffix
- [ ] Catch-all `"**"` handler defined last
- [ ] Witz templates named in .upp file list
- [ ] Database: threads=1 for SQLite3, WorkThread override
- [ ] Build successful, test at http://localhost:8001/myapp
