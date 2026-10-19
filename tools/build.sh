#!/usr/bin/env bash
# build.sh <build-dir> <targets...>  -- builds RelWithDebInfo, prints errors and
# warnings except upstream's size_t noise (C4267) and the temp-dir MSB8029.
B="$1"; shift
CM="/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
T=()
for t in "$@"; do T+=(--target "$t"); done
LOG="$B/last_build.log"
MSYS_NO_PATHCONV=1 "$CM" --build "$B" --config RelWithDebInfo --parallel "${T[@]}" -- -v:m -nologo > "$LOG" 2>&1
RC=$?
grep -E "error|warning" "$LOG" | grep -v -E "C4267|MSB8029|LNK4099" | sed -E 's/ \[[^]]*\]$//' | sort -u | head -60
echo "BUILD EXIT=$RC"
exit $RC
