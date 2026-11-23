#!/usr/bin/env bash
# Host tests of the patch code in this repository (tanh3/Tanh3Patch.hpp, AcdcOutput.h, AcdcInput.h,
# schliere/: all Schliere variants, the two LED measurement patches in test/). Compiles each
# source unchanged against a minimal stand-in for the OwlProgram API (test/Patch.h).
#   dsp_test.cpp       Tanh3 and limiter: levels, saturation, antialiasing, mix, CV
#   schliere_test.cpp  Schliere: transfer function against the textbook allpass, group
#                      delay, notches, feedback, DC, CV scaling, stability
#   blockrate_test.cpp Block-rate canceller: learning, DC, slow CV, loud signals, a tone at fs/8
#   ledtest_test.cpp   LED test patch (test/SchliereLedTestPatch.hpp): schedule of the LED states
#   ledpath_test.cpp   LED path test patch (test/SchliereLedPathPatch.hpp): schedule, outputs per
#                      phase, the measurement at the inputs
#
# Usage: test/run.sh        Needs a C++17 compiler (CXX, default c++). Exit code 0 = all cases ok.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CXX="${CXX:-c++}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/owldsp.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
STATUS=0
for T in dsp_test schliere_test blockrate_test ledtest_test ledpath_test; do
  "$CXX" -std=c++17 -O1 -Wall -I"$ROOT/test" -I"$ROOT" -I"$ROOT/tanh3" -I"$ROOT/schliere" -o "$WORK/$T" "$ROOT/test/$T.cpp" 2> "$WORK/cc.log" \
    || { grep -v xcrun_db "$WORK/cc.log" >&2; echo "Compile failed: $T" >&2; exit 1; }
  echo "== $T"
  "$WORK/$T" || STATUS=1
done
exit $STATUS
