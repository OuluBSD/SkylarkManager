#!/bin/sh
# Normalization used before comparing captures. Writes the normalized form of
# <file> to stdout. Bodies and headers take the same rules; Date and Set-Cookie
# headers are dropped because they carry the clock and a random session id.
#   - Date:, Set-Cookie: header lines removed
#   - usr_<hex> user ids -> usr_X
#   - dd/mm/yyyy hh:mm:ss timestamps -> TS
#   - relative times ("13 seconds ago", "2 hours ago") -> AGO
#   - Content-Length removed (follows the normalized body)
#   - the harness temp directory name (ms-capture.<random>) -> ms-capture.X
sed -E \
	-e '/^[Dd]ate:/d' \
	-e '/^[Ss]et-[Cc]ookie:/d' \
	-e '/^[Cc]ontent-[Ll]ength:/d' \
	-e 's/usr_[0-9a-f]+/usr_X/g' \
	-e 's/ms-capture\.[A-Za-z0-9]+/ms-capture.X/g' \
	-e 's/[0-9]{2}\/[0-9]{2}\/[0-9]{4} [0-9]{2}:[0-9]{2}:[0-9]{2}/TS/g' \
	-e 's/[0-9]+ (second|minute|hour|day|week|month|year)s? ago/AGO/g' \
	"$1"
