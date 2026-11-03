# Upp::Value — Universal Value Type

## Purpose

`Upp::Value` is a discriminated-union type that can hold many different types — scalars, strings, dates, containers, and even custom classes. It is conceptually similar to a **JavaScript Object** or a **Python object**: one type, many possible payloads, runtime type inspection.

**Reference**: `reference/Value/Value.cpp`
**Core**: `uppsrc/Core/Value.h`, `uppsrc/Core/ValueUtil.h` (ValueArray/ValueMap)

## Why GUI Classes Take Value Arguments

Widgets in `uppsrc/CtrlLib` accept `Value` arguments on purpose:

1. **Late text conversion** — converting to text happens as close to the GUI as possible; no premature `AsString()` at the data source.
2. **Correct sorting** — a list column sorts properly from the *typed* value (dates as dates, numbers as numbers). Sorting text-converted values gives `"10" < "9"` nonsense.
3. **One function, many types** — the same `SetData(Value)` accepts int, String, Date, or any custom class whose Value conversion is implemented.

```cpp
ArrayCtrl list;
list.AddColumn("Name");
list.AddColumn("Modified");
list.AddColumn("Size");

list.Add("report.txt", GetSysTime(), 15423);   // String, Time, int — all Value
list.SetOrder(2);                              // sorts numerically, not lexically
```

## Built-in Types

| Type | Constant | Notes |
|------|----------|-------|
| `int` | `INT_V` | |
| `int64` | `INT64_V` | |
| `double` | `DOUBLE_V` | |
| `float` | `FLOAT_V` | |
| `bool` | `BOOL_V` | |
| `String` | `STRING_V` | |
| `WString` | `WSTRING_V` | |
| `Date` | `DATE_V` | |
| `Time` | `TIME_V` | |
| `ValueArray` | `VALUEARRAY_V` | Vector of Values |
| `ValueMap` | `VALUEMAP_V` | Key→Value map |
| Error | `ERROR_V` | via `ErrorValue("text")` |
| Null | (special) | `Null` — typed empty |
| Geometric | — | `Point`, `Rect`, `Color`, `Size` etc. are Value-compatible |
| Custom | `0x8000000+` | registered types |

## Basic Usage

```cpp
#include <Core/Core.h>
using namespace Upp;

Value v;
v = 42;                    // int
v = 12.5;                  // double
v = "text";                // String
v = GetSysDate();          // Date
v = ErrorValue("failed");  // error
v = Null;                  // null

// Type checks
if(IsNull(v))       ...
if(IsError(x))      String err = GetErrorText(x);
if(IsString(v))     String s = v;
if(IsNumber(v))     double d = v;         // int, double, float, int64, bool
if(IsDateTime(v))   ...                   // Date or Time

// Conversions (operators)
String s = v;      // ToString of content
double d = v;
int    i = v;
Date   dt = v;

// Raw type number
dword t = v.GetType();
```

## ValueArray and ValueMap — Nested Structures

Like JavaScript arrays/objects:

```cpp
ValueArray va;
va.Add(1);
va.Add("Hello");
va.Add(GetSysDate());
va.Add(3.14);

ValueMap m;
m.Add("name", "Seppo");
m.Add("age", 42);
m.Add("tags", va);               // nested container

// Access
int n = va.GetCount();
Value first = va[0];
String name = m["name"];
ValueMap mm(v);                  // from a Value holding a map

// Iteration
for(int i = 0; i < va.GetCount(); i++) ...
for(int i = 0; i < m.GetCount(); i++)
    Cout() << m.GetKey(i) << " = " << m.GetValue(i);
```

This is exactly the structure used by Skylark templates (`$for(i in PERSON)`, `$i.NAME`) and AppRegistry's `registry.json`.

## Custom Types — Two Flavors

### Flavor A: RawToValue (simple storage)

For plain structs that just need to travel inside a Value:

```cpp
struct RawFoo {
    String a;
    int    b;
};

Value v = RawToValue(y);                  // store
if(IsTypeRaw<RawFoo>(v)) {                // detect
    const RawFoo& r = v.To<RawFoo>();     // access (reference into Value)
}
```

Minimal requirements: the type must be movable. No registration needed. No ordering/serialization support — storage only.

### Flavor B: RichValue (full-featured)

For types that need comparison (sorting!), serialization, hashing, and proper ToString — the full protocol:

```cpp
struct RichFoo : ValueType<RichFoo, 513, Comparable<RichFoo, Moveable<RichFoo>>> {
    String a;
    int    b;

    int      Compare(const RichFoo& o) const { return CombineCompare(a, o.a)(b, o.b); }
    void     Serialize(Stream& s)            { s % a % b; }
    String   ToString() const                { return String().Cat() << a << ", " << b; }
    unsigned GetHashValue() const            { return CombineHash(a, b); }
    bool     IsNullInstance() const          { return IsNull(a); }

    operator Value() const                   { return RichValue<RichFoo>(*this); }
    RichFoo(const Value& v)                  { *this = v.To<RichFoo>(); }
    RichFoo()                                {}
};

INITBLOCK
{
    Value::Register<RichFoo>();            // enable (de)serialization by type name
}
```

Usage:

```cpp
RichFoo x;  x.a = "SOMETHING"; x.b = 123456;
Value v = x;                              // via operator Value()

if(IsType<RichFoo>(v)) {                  // detect
    RichFoo back = v;                     // via constructor from Value
}

// Serialization round-trip works because of Serialize + Register:
String bindata = StoreAsString(x);
RichFoo xx;
LoadFromString(xx, bindata);
```

**Type number** (`513` above): pick a unique constant < 0x8000000 for your app; without one, `StaticTypeNo<T>() + 0x8000000` is auto-assigned (fine for same-process use, but not stable across binaries for serialization).

**What each method buys you:**

| Method | Enables |
|--------|---------|
| `Compare` | Sorting in ArrayCtrl, Min/Max, ordered maps |
| `Serialize` | `StoreAsString`/`LoadFromString`, stream storage |
| `ToString` | GUI display, logging, `AsString(v)` |
| `GetHashValue` | Use as key in Index/HashMap-based structures |
| `IsNullInstance` | Proper `IsNull(v)` semantics |
| `operator Value` / ctor | Implicit conversion both directions |

## Detection and Dispatch Pattern

The reference's `Print` function is the canonical dispatch:

```cpp
void Print(const Value& x)
{
    if(IsError(x))           { /* GetErrorText(x) */ }
    else if(IsNull(x))       { /* empty */ }
    else if(IsString(x))     { String s = x; }
    else if(IsNumber(x))     { double d = x; }
    else if(IsDateTime(x))   { Date d = x; }
    else if(IsType<RichFoo>(x))   { RichFoo f = x; }
    else if(IsTypeRaw<RawFoo>(x)) { const RawFoo& r = x.To<RawFoo>(); }
    else                     { Cout() << x.GetType(); }  // unknown type number
}
```

Helper predicates: `IsIntegerValueTypeNo`, `IsFloatValueTypeNo`, `IsStringValueTypeNo`, `IsDateTimeValueTypeNo` (for type numbers).

## Null vs Error vs Void

| State | Meaning | Create | Detect |
|-------|---------|--------|--------|
| Null | Valid "no value" | `Null`, `(int)Null` | `IsNull(v)` |
| Error | Failure with text | `ErrorValue("msg")` | `IsError(v)` |
| Void | Uninitialized | `Value()` | `v.IsVoid()` |

Null is *typed* (`(int)Null` vs `(String)Null`) — an int column can hold Null and still sort correctly. Error propagates: functions receiving Error typically return Error.

## Serialization

RichValue types with `Serialize` + `Register` round-trip through text and binary:

```cpp
String s = StoreAsString(x);
LoadFromString(y, s);

String j = StoreAsJson(x);       // for Jsonize-capable content
LoadFromJson(y, j);
```

`ValueMap`/`ValueArray` serialize natively to JSON — this is how AppRegistry stores state.

## Performance Notes

- Value is a **small-type optimized** union: scalars (int, double, Date...) are stored inline; String/containers use a heap-backed shared representation
- Copying a Value of scalar type is trivial; String/containers are refcounted (cheap copy)
- Type check + conversion in hot loops is fine; repeated `ToString()` in paint loops is not — cache
- Custom RichValue payloads are heap-allocated once per value

## Common Pitfalls

1. **Premature AsString** — passing text to GUI loses sorting/formatting; pass the typed Value
2. **Missing Register** — serialization of custom RichValue fails without `Value::Register<T>()` in INITBLOCK
3. **Type number collision** — two custom types with the same explicit number corrupt each other; keep an app-level registry of numbers
4. **Comparing mixed types** — `v1 == v2` across different types is false even if "textually equal"; convert first
5. **Assuming IsNumber includes Date** — it doesn't; use `IsDateTime` separately
6. **RawToValue sorting** — raw values can't Compare; use RichValue if the column must sort

## Integration with Other Skills

- **gui-widget-reference** — ArrayCtrl cells, EditField, DropList all take Value; custom Display renders typed Values
- **app-registry** — registry stores `Value` per key; JSON state via ValueMap
- **skylark-web-ui** — template variables are Values; `$for` iterates ValueArray/ValueMap
- **toon-file-format** — Value trees serialize to structured formats
- **action-planner** — atom states and action parameters can be Values for flexible AI introspection
- **memory-safe-containers** — ValueArray/ValueMap internally manage their storage; treat like any container re: references

## Checklist

- [ ] GUI-bound data passed as typed Value, not pre-stringified
- [ ] Custom types: RawToValue for storage-only, RichValue for sortable/serializable
- [ ] RichValue implements Compare/Serialize/ToString/GetHashValue/IsNullInstance + both conversions
- [ ] `Value::Register<T>()` in INITBLOCK for serialized custom types
- [ ] Unique type numbers tracked app-wide
- [ ] Dispatch uses IsNull/IsError/IsString/IsNumber/IsDateTime/IsType chain
- [ ] Null kept typed (`(int)Null`) for columnar data

## Summary

`Upp::Value` is the lingua franca between data and GUI in U++: one uniform type, runtime type inspection, late text conversion, correct typed sorting, and extensibility to custom classes via RawToValue (simple) or RichValue (full protocol). Think JavaScript Object / Python object — with C++ type safety at the boundaries.
