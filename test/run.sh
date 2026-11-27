#!/usr/bin/env bash
# Host tests of the patch code in this repository (patches/tanh3/Tanh3Patch.hpp, include/AcdcOutput.h,
# include/AcdcInput.h, patches/schliere/: all Schliere variants, patches/plateau/, the two LED measurement
# patches in test/). Compiles each source unchanged against a minimal stand-in for the OwlProgram
# API (test/Patch.h).
#   dsp_test.cpp       Tanh3 and limiter: levels, saturation, antialiasing, mix, CV
#   schliere_test.cpp  Schliere: transfer function against the textbook allpass, group
#                      delay, notches, feedback, DC, CV scaling, stability
#   blockrate_test.cpp Block-rate canceller: learning, DC, slow CV, loud signals, a tone at fs/8
#   ledtest_test.cpp   LED test patch (test/SchliereLedTestPatch.hpp): schedule of the LED states
#   ledpath_test.cpp   LED path test patch (test/SchliereLedPathPatch.hpp): schedule, outputs per
#                      phase, the measurement at the inputs
#   plateau_test.cpp   Plateau: memory, RT60 against Space, freeze, gate/clear at in 4, flush,
#                      mix, normalling of in 2, morph, size sweeps, block sizes, LEDs
#   plateau_profile_test.cpp  Plateau measurement patch (test/PlateauProfilePatch.hpp): messages,
#                      bookkeeping of the cycle probes
#   plateau_ref_test.cpp  Plateau core against the original (ValleyRackFree 2.4.5, double), in
#                      double and in float. Only with VALLEY_SRC=<ValleyRackFree>/src, skipped otherwise
#
# Usage: test/run.sh        Needs a C++17 compiler (CXX, default c++). Exit code 0 = all cases ok.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CXX="${CXX:-c++}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/owldsp.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
STATUS=0
for T in dsp_test schliere_test blockrate_test ledtest_test ledpath_test plateau_test plateau_profile_test; do
  "$CXX" -std=c++17 -O1 -Wall -I"$ROOT/test" -I"$ROOT/include" -I"$ROOT/patches/tanh3" -I"$ROOT/patches/schliere" -o "$WORK/$T" "$ROOT/test/$T.cpp" 2> "$WORK/cc.log" \
    || { grep -v xcrun_db "$WORK/cc.log" >&2; echo "Compile failed: $T" >&2; exit 1; }
  echo "== $T"
  "$WORK/$T" || STATUS=1
done
if [ -n "${VALLEY_SRC:-}" ] && [ -f "$VALLEY_SRC/Plateau/Dattorro.cpp" ]; then
  for S in double float; do
    "$CXX" -std=c++17 -O2 -w -DPLATEAU_SAMPLE=$S -I"$ROOT/test" -I"$ROOT" -I"$VALLEY_SRC" -o "$WORK/plateau_ref_$S" \
      "$ROOT/test/plateau_ref_test.cpp" "$VALLEY_SRC/Plateau/Dattorro.cpp" "$VALLEY_SRC/dsp/filters/OnePoleFilters.cpp" \
      2> "$WORK/cc.log" || { grep -v xcrun_db "$WORK/cc.log" >&2; echo "Compile failed: plateau_ref_test" >&2; exit 1; }
    echo "== plateau_ref_test ($S)"
    "$WORK/plateau_ref_$S" || STATUS=1
  done
else
  echo "== plateau_ref_test skipped (set VALLEY_SRC to the src folder of ValleyRackFree 2.4.5)"
fi
if command -v node >/dev/null; then
  echo "== web_protocol_test"
  # WEB_REF_BUILDS="dir1 dir2": OwlProgram build folders with patch.bin and <Name>.syx
  # shellcheck disable=SC2086
  node "$ROOT/test/web_protocol_test.mjs" ${WEB_REF_BUILDS:-} | grep -v '^ok ' || true
  # shellcheck disable=SC2086
  node "$ROOT/test/web_protocol_test.mjs" ${WEB_REF_BUILDS:-} >/dev/null || STATUS=1
else
  echo "== web_protocol_test skipped (needs node)"
fi
exit $STATUS
