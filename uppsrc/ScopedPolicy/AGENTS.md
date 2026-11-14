# ScopedPolicy Package Guide

`ScopedPolicy` contains pure content-policy data types, trusted-registry validation and parsing, identity handling, ancestry evaluation, redacted policy results, and synthetic self-tests. It depends only on U++ `Core`.

Application configuration loading, request/session enforcement, and app-specific adapters belong in the consuming application package. The dependency direction is one-way: applications may depend on `ScopedPolicy`; `ScopedPolicy` must never depend on an application.

Secret references and provider configuration revisions are accepted only as validated metadata. They are never included in `EffectivePolicy::ToRedactedValue()` or other public policy output. Keep provider revision state private to `EffectivePolicy` and do not expose secret material through diagnostics or tests.
