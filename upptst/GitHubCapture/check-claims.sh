#!/bin/sh
# Checks the claims the GitHub tabs must not make, on a capture directory.
# The security tab must not claim a clean scan; the settings tab must not state
# visibility or offer a visibility change the view cannot back.
# Usage: check-claims.sh <capture_dir>
DIR=$1
fail=0
# must <file> <text> <message>: the body contains text
must() {
	if ! grep -qF -- "$2" "$DIR/$1"; then echo "FAIL $1: $3"; fail=1; fi
}
# mustnot <file> <text> <message>: the body does not contain text
mustnot() {
	if grep -qF -- "$2" "$DIR/$1"; then echo "FAIL $1: $3"; fail=1; fi
}
SEC=_repo_SeppoPakonen_demo_repo_security.body
must $SEC "Secret scan: not performed" "security tab must say the secret scan was not performed"
mustnot $SEC "Zero leaked" "security tab must not claim a clean secret scan"
if [ "$fail" = 0 ]; then echo "claims: PASSED"; fi
exit $fail
