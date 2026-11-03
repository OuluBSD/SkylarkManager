## U++ Object Semantics: Moveable & Ownership

### Moveable Requirement

- All classes and structs used in NTL containers (`Vector`, `Array`, etc.) MUST inherit from `Moveable<T>`.
- NTL containers use raw memory relocation (memcpy-style moves) when growing; a non-Moveable member type inside such a container is a latent corruption bug.

### Ownership Policy

- Prefer ownership via `One<>` / `Array<T>` with a clear single owner.
- Avoid `std::shared_ptr` and `std::weak_ptr` — they hide lifetimes and defeat deterministic destruction.
- Use `Ptr<>` only when the target class inherits `Pte<>`: a `Ptr` is nulled automatically if the object is destroyed, which prevents dangling pointers.
- `One<T>` owns exclusively; transferring requires explicit `Pick()` / `std::move`.

Related: `memory-pooling.md` (RecyclerPool/BiVectorRecycler), `ai/skills/memory-safe-containers.md` (Array/ArrayMap vs Vector/VectorMap address stability).
