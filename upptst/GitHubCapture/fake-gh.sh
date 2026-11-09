#!/bin/sh
# Stands in for the gh CLI during capture. It never reaches the network and
# always fails, so every live GitHub view takes its "unavailable" path, the
# same on every run.
echo "fake gh: unavailable in capture harness" >&2
exit 1
