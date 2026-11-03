# Memory Problem Avoidance — Proper U++ Container Usage

## Purpose

This skill prevents the most common U++ memory bugs by choosing the correct container/ownership template for each situation. Wrong container choice — especially `Vector` where `Array` belongs — is a guaranteed source of strange, hard-to-debug crashes.

**References**: `reference/Ptr/`, `uppsrc/Core/Ptr.h`, `uppsrc/Core/Vcont.h` (One), `uppsrc/Core/Other.h` (Single), `uppsrc/Core/LinkedList.h`, `uppsrc/Core/Shared.h`

## Decision Table

| Situation | Use | NOT |
|-----------|-----|-----|
| Default ownership | **RAII** (stack member / value semantics) | Smart pointers |
| Shared ownership needed | `Shared<T>` (works, but avoid) | — prefer redesign to RAII |
| Create later than constructor | `One<T>` (Create/Clear) | raw `new` member |
| Must know when pointee dies | `Ptr<T>` + `Pte<T>` base | raw pointers |
| Pointers/refs to many elements | `Array<T>` | **`Vector<T>`** — addresses move! |
| Key→value with stable element addresses | `ArrayMap<K,T>` | **`VectorMap<K,T>`** — same problem |
| Hot path only, single-threaded access | `Vector<T>` / `VectorMap<K,T>` | — (acceptable there) |
| One instance for program lifetime | `Single<T>()` | global variables, lazy singletons |
| Frequent birth/death, order changes | `LinkedList<T>` | Vector with mid-removals |
| Same, on hot codepath | `LinkedList<T, true>` (recycler) | anything heap-allocating |

## Rule 1: Prefer RAII Over Smart Pointers

`Shared<T>` works (reference-counted, thread-safe refcount via `Atomic`), but **smart pointers are not RAII-friendly** and should not be the first choice:

```cpp
// PREFERRED: RAII — object lifetime tied to scope/member
struct Engine {
    Renderer  renderer;     // member — constructed/destroyed with Engine
    One<Pool> pool;         // created later (see Rule 2)
};

// AVOID unless genuinely shared:
Shared<Renderer> renderer;  // Who owns it? When does it die? Unclear.
```

Smart pointers hide ownership semantics, make destruction order unpredictable, and complicate debugging. Use `Shared<T>` only when ownership is *genuinely* shared and cannot be expressed by structure.

## Rule 2: Late Creation → One<T>

When an object must be created after the constructor (config not ready, lazy init, optional component):

```cpp
struct Engine {
    One<Renderer> renderer;   // empty until Create()

    void StartGraphics() {
        renderer.Create();              // new Renderer()
        renderer.Create<Renderer>(args);  // with constructor args
    }
    void StopGraphics() {
        renderer.Clear();               // delete + null
    }
};

// Checks:
if(engine.renderer)              // operator bool
if(!engine.renderer.IsEmpty())   // explicit
engine.renderer->Draw();         // operator->
```

`One<T>` API: `Create()`, `Create<TT>(args...)`, `Clear()`, `IsEmpty()`, `operator->`, `operator*`, `Attach(ptr)`, `Detach()`. Deleted automatically when the `One` is destroyed. Copy is deleted (move + deep-copy-with-`clone` only).

## Rule 3: Death Notification → Ptr + Pte

When you hold a pointer and must know when the pointee is destroyed (avoid zero-address reads / use-after-free):

**Reference**: `reference/Ptr/main.cpp`

```cpp
struct Foo : Pte<Foo> {           // pointee inherits Pte<Foo>
    String text;
};

Ptr<Foo> ptr;                     // "weak" pointer
{
    Foo foo;
    foo.text = "Text";
    ptr = &foo;
    Cout() << ptr->text;          // works
}                                 // foo destroyed
// ptr is now automatically NULL — safe to check:
if(ptr)      // false — no crash
    ptr->text;
```

- `Pte<T>` base gives the object a refcounted "prec" record
- `Ptr<T>` becomes NULL automatically when the pointee dies
- Cost: one atomic refcount per Pte object; small overhead, huge safety win
- Use for: callbacks into objects that may die, cross-subsystem references, pending async operations

## Rule 4: Element Addresses — Array vs Vector

**THE critical rule.** `Vector<T>` reallocates on growth — all element addresses change. `Array<T>` keeps elements at stable addresses (pointer-holders move, payloads don't).

```cpp
// DANGEROUS:
Vector<Widget> widgets;
widgets.Add().WhenAction = THISBACK(OnClick);
// ... later:
widgets.Add();   // ← REALLOCATES. OnClick's captured &widgets[0] is now dangling.
// Result: weird crash hours later, in unrelated code. "Outoja ongelmia."

// SAFE:
Array<Widget> widgets;
widgets.Add().WhenAction = THISBACK(OnClick);
widgets.Add();   // element addresses unchanged — references stay valid
```

**Why it bites**: any function taking `T&` or `T*` stores that address. If the Vector later grows, has elements removed from the middle, or gets moved, the stored address silently dangles. The crash appears far from the cause.

### Same rule applies to maps

| Need stable addresses | Container |
|----------------------|-----------|
| Yes | `ArrayMap<K, T>` |
| No (pure hot-path lookup) | `VectorMap<K, T>` |

`VectorMap` has the identical reallocation behavior as `Vector` — taking a reference to `vmap.GetAdd(key)` and then inserting more keys dangles the reference.

### When Vector IS acceptable

`Vector<T>`/`VectorMap<K,T>` are fine **only** when:
- Elements are used strictly inside a hot codepath (compute, discard)
- No pointers/references/iterators escape that scope
- No other thread can grow/modify the container meanwhile

**Thread caveat**: even in hot-path use, if another thread can `Add()` to the Vector, it may relocate memory mid-read. Then you need a `Mutex` or `SpinLock` around all access — at which point `Array` was probably simpler and safer.

```cpp
// Acceptable Vector use:
double ComputeSum(const Vector<double>& v) {
    double s = 0;
    for(double x : v) s += x;   // no address escapes
    return s;
}
```

## Rule 5: Program-Lifetime Singleton → Single<T>

When exactly one instance is needed for the whole program run:

```cpp
struct GlobalConfig {
    String theme = "dark";
    int    workers = 4;
};

// Anywhere in code:
Single<GlobalConfig>().theme = "light";

// With constructor args:
struct Logger { Logger(const String& path) {...} };
Single<Logger>("app.log").Write("...");
```

- Thread-safe static local initialization (C++11 guarantee)
- Constructed on first use, destroyed at exit
- **Preferred over global variables** — avoids static init order fiasco, clarifies intent, lazy
- See existing uses: `Single<AppFlags>()`, `Single<ValidHtmlTags>()`

## Rule 6: Frequent Birth/Death + Order Changes → LinkedList

When objects are created and destroyed often and their order changes frequently:

```cpp
LinkedList<Task> tasks;
auto it = tasks.InsertTail();
it->name = "render";
// reorder, remove from middle — all O(1), no element movement
tasks.Remove(it);
tasks.InsertHead(other);
```

Elements never move in memory — iterators/pointers stay valid across insertions/removals elsewhere.

### Hot codepath → LinkedList<T, true> (recycler)

The second template parameter enables `RecyclerPool` internally:

```cpp
LinkedList<Packet, true> queue;   // keep_as_constructed=true
```

With `true`:
- Removed items return to a `RecyclerPool<Item, true>` instead of being freed
- **Constructor/destructor are NOT called on recycle** — internal `Vector`/`String` capacity is retained
- Eliminates malloc/free churn on the hot path
- Pool is thread-safe (internally synchronized)

This matches the `RecyclerPool`/`BiVectorRecycler` guidance in AGENTS.md — same family, same capacity-retention benefit.

## Container Cheat Sheet

| Container | Address stability | Best for |
|-----------|------------------|----------|
| `Vector<T>` | ❌ moves on growth | Hot-path compute, no escaping refs |
| `Array<T>` | ✅ stable | Anything referenced from elsewhere |
| `VectorMap<K,T>` | ❌ moves on growth | Hot-path key lookup |
| `ArrayMap<K,T>` | ✅ stable | Keyed objects referenced elsewhere |
| `LinkedList<T>` | ✅ stable | Frequent insert/remove/reorder |
| `LinkedList<T,true>` | ✅ stable + recycled | Same on hot path |
| `One<T>` | single object | Late/optional creation |
| `Single<T>()` | program lifetime | Global unique instance |
| `Ptr<T>`/`Pte<T>` | tracks death | Weak references, callbacks |
| `Shared<T>` | ref-counted | Last resort when RAII impossible |

## Common Bug Patterns to Recognize

```cpp
// BUG 1: reference escapes growing Vector
Foo& f = vec.Add();
vec.Add();        // f dangles
f.DoSomething();  // UB

// BUG 2: pointer stored across mutation
Bar* b = &vec[3];
vec.Remove(0);    // b dangles

// BUG 3: map ref + insert
Val& v = vmap.GetAdd(key);
vmap.GetAdd(other);  // v may dangle

// BUG 4: cross-thread Vector growth
// thread A: reads vec[i]; thread B: vec.Add() → relocation → A reads freed memory
// FIX: Array<T>, or Mutex/SpinLock, or snapshot

// BUG 5: smart pointer where RAII suffices
Shared<Engine> e;  e.Create();   // why not just a member?
```

## Integration with Other Skills

- **high-performance-memory-optimization** — RecyclerPool/BiVectorRecycler details, profiling hot allocations
- **gui-low-complexity / gui-medium-complexity** — widget vectors: `Vector<Ctrl>` children are managed by parent Ctrl (special case), but your own data models still follow these rules
- **multi-agent-coordination** — shared state containers between agents: use `Array`/`ArrayMap` + locks, never `Vector` refs across agents
- **action-planner** — atom/action collections: `Array` if referenced by index from saved plans

## Checklist

- [ ] No escaping pointers/refs into `Vector`/`VectorMap` elements
- [ ] Late-created members use `One<T>`
- [ ] Death-aware references use `Ptr`/`Pte`
- [ ] Program-lifetime singletons use `Single<T>()`
- [ ] Churn-heavy collections use `LinkedList<T>`, hot ones `LinkedList<T,true>`
- [ ] Smart pointers only where ownership is genuinely shared
- [ ] Cross-thread container access is locked OR uses address-stable containers with locked structural changes
- [ ] No `Shared<T>` where a plain member would do
