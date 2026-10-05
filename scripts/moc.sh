#!/usr/bin/env bash
# Run Qt's meta-object compiler over the headers in src/core and src/app.
#
# premake has no idea what Qt is, and teaching it via per-file custom build rules
# means reproducing the compiler's include paths and flags in Lua -- two places
# that then have to agree forever. So this does the CMake AUTOMOC thing instead:
# every header with a Q_OBJECT gets a moc_<name>.cpp generated here, and the
# matching <name>.cpp #includes it at the bottom. The moc output is then just
# part of a translation unit premake already knows how to compile, and there is
# nothing to keep in sync.
#
# moc writes its #include of the original header as a path relative to the output
# file, and a quoted include inside a generated file resolves relative to that
# generated file -- so the path works without any -I at all.
#
# Usage: scripts/moc.sh <output-dir>
set -uo pipefail

# Resolve the output directory against the *caller's* working directory before
# moving to the project root. premake runs this from the build directory with a
# relative path, and resolving that after the cd silently writes the generated
# files into the source tree instead -- where nothing includes them, so the only
# symptom is a missing-file error naming a path that looks correct.
OUT="${1:?usage: moc.sh <output-dir>}"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"

HERE="$(cd "$(dirname "$0")/.." && pwd)"
cd "$HERE"

MOC="${MOC:-}"
if [ -z "$MOC" ]; then
    for candidate in /usr/lib/qt6/moc moc-qt6 moc; do
        if command -v "$candidate" >/dev/null 2>&1; then MOC="$candidate"; break; fi
    done
fi
[ -n "$MOC" ] || { echo "moc.sh: no Qt moc found -- install qt6-base" >&2; exit 1; }

# A header without Q_OBJECT has no meta-object and moc would emit an empty file
# that the matching .cpp then fails to find a use for.
status=0
for header in src/core/*.h src/app/*.h; do
    [ -e "$header" ] || continue
    grep -q "Q_OBJECT" "$header" || continue

    base="$(basename "$header" .h)"
    target="$OUT/moc_$base.cpp"
    tmp="$target.tmp"

    if ! "$MOC" "$header" -o "$tmp"; then
        echo "moc.sh: failed on $header" >&2
        rm -f "$tmp"
        status=1
        continue
    fi

    # Only replace when the contents change. moc stamps nothing time-based, so an
    # unconditional write would make every build rebuild every GUI object file
    # for no reason.
    if [ -f "$target" ] && cmp -s "$tmp" "$target"; then
        rm -f "$tmp"
    else
        mv -f "$tmp" "$target"
    fi
done

# A stale moc_*.cpp for a header that has been deleted or lost its Q_OBJECT is
# not harmless: it still compiles, and it keeps referring to a class that may no
# longer exist.
for generated in "$OUT"/moc_*.cpp; do
    [ -e "$generated" ] || continue
    base="$(basename "$generated" .cpp)"
    base="${base#moc_}"
    src=""
    for dir in src/core src/app; do [ -f "$dir/$base.h" ] && src="$dir/$base.h"; done
    if [ -z "$src" ] || ! grep -q "Q_OBJECT" "$src"; then
        rm -f "$generated"
    fi
done

exit $status
