#!/bin/sh
# Builds deterministic git fixtures under <dev_dir>: one repo per name, fixed
# author, dates and content, so commit ids are the same on every run.
# Usage: make-fixtures.sh <dev_dir>
set -eu
DEV=$1
export TZ=UTC LANG=C
GIT="git -c user.name=Fixture -c user.email=fixture@example.invalid -c init.defaultBranch=master"

repo() {
	mkdir -p "$DEV/$1"
	cd "$DEV/$1"
	$GIT init -q
}

commit() {
	# commit <date> <message> ; content is written by the caller first
	GIT_AUTHOR_DATE="$1" GIT_COMMITTER_DATE="$1" $GIT add -A
	GIT_AUTHOR_DATE="$1" GIT_COMMITTER_DATE="$1" $GIT commit -q -m "$2"
}

repo demo-repo
printf '# Demo repo\n\nFixture for the GitHub capture harness.\n' > README.md
mkdir -p src
printf 'int main() { return 0; }\n' > src/main.cpp
commit "2026-11-01T10:00:00Z" "Initial import"
printf 'int main() { return 1; }\n' > src/main.cpp
commit "2026-11-02T11:30:00Z" "Change exit status"
printf '\n## Usage\n\nRun it.\n' >> README.md
commit "2026-11-03T09:15:00Z" "Document usage"

repo second-repo
printf 'Second fixture repo.\n' > NOTES.txt
commit "2026-11-04T08:00:00Z" "Add notes"

# Plain (uncompressed) TPP topics for the documentation preview route. The
# preview reads <dev>/ai-upp/uppsrc/Core/srcdoc.tpp/*.tpp, so the harness's
# DEV_DIR points it here and never at a real checkout.
mkdir -p "$DEV/ai-upp/uppsrc/Core/srcdoc.tpp"
printf 'TITLE("Fixture alpha")\n\n&\nAlpha paragraph one.\n&\nAlpha paragraph two.\n' > "$DEV/ai-upp/uppsrc/Core/srcdoc.tpp/alpha.tpp"
printf 'TITLE("Fixture beta")\n\n&\nBeta body text.\n' > "$DEV/ai-upp/uppsrc/Core/srcdoc.tpp/beta.tpp"
