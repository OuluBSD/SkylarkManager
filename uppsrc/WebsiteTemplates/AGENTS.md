# WebsiteTemplates Package Guide

`WebsiteTemplates` owns reusable website template configuration and the renderer
for its page shell. Site packages look up a template by stable id and provide
page-specific values; CSS and shared HTML shells belong here, not in site route
implementation files.

## Templates and themes

- `WebsiteTemplates.h/.cpp` defines `WebsiteTemplate`, the built-in registry,
  template render methods, and shared host theme/density helpers.
- Built-in ids are `meta-login`, `glassmorphism` and `engine-login`; styles use system font
  stacks and have no external CDN dependencies.
- `RegisterWebsiteTemplate` rejects empty and duplicate ids and never replaces
  an existing configuration. Register additional templates during startup,
  before request handlers read the registry.
- `engine-login` owns both the password-form and SSO-only login body pieces;
  `SessionRoutes` supplies route values and chooses the SSO-only variant.
- Theme ids and density ids are allowlisted to match WebFramework preference
  cookies. `WebsiteThemeHeadScript()` preserves task 052 same-host message
  propagation and only updates whitelisted document attributes.
- Add template behavior as configuration and renderer logic here. Do not put
  CSS or whole HTML page shells in consumer `.cpp` files.

## Shared composed-page look (task 052 follow-up)

- `WebsitePageStyles()` is the `<style>` every composed webapp page puts in
  its head: theme tokens for all seven host themes (`vivid-dark` default),
  density tokens (`dense` default) and a base look (body, nav, forms, tables,
  messages). Pages keep only page-specific rules after it.
- `WebsiteThemeControlsHtml()` renders the Theme and Density buttons for a nav
  or header; `WebsiteThemeControlsScript()` cycles them, writes the
  host-wide `wf_theme` / `wf_density` cookies and sets `<html>` attributes. It
  runs on `DOMContentLoaded`, so it can sit in head or at the end of body.
- Theme tokens are CSS variables: `--bg --panel --fg --muted --line --accent
  --control --sea`, status colours `--st-*`, density `--pad --gap --fs`. A new
  page styles itself with these, never with `light`/`dark` selectors.
