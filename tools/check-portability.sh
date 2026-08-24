#!/bin/sh
# src/core and src/hal must never include a platform header.
#
# This is the invariant the whole project rests on: it is why the pipeline runs
# on a laptop, why layout can be iterated a hundred times a day, and why a port
# to another board is six interfaces rather than a rewrite. It is also the
# easiest rule in the codebase to break by accident, so CI checks it.
set -eu

fail=0

# Platform headers that must not appear above the HAL.
pattern='#[[:space:]]*include[[:space:]]*[<"]\(Arduino\.h\|Inkplate\.h\|WiFi\.h\|SD\.h\|SPI\.h\|Wire\.h\|esp_[a-z_]*\.h\|freertos/\)'

for dir in src/core src/hal; do
  if grep -rn "$pattern" "$dir" 2>/dev/null; then
    echo "error: $dir includes a platform header (see above)" >&2
    echo "       Hardware belongs behind an interface in src/hal/hal.h." >&2
    fail=1
  fi
done

# src/core must not reach into the HAL either, with one deliberate exception:
# the reader is the top of the portable stack and drives the HAL by design.
if grep -rn '#[[:space:]]*include[[:space:]]*"hal/' src/core \
     --include='*.h' --include='*.cpp' 2>/dev/null \
     | grep -v '^src/core/ui/reader\.' ; then
  echo "error: src/core includes hal/ outside src/core/ui/reader" >&2
  echo "       Only the reader drives the HAL; everything else takes data." >&2
  fail=1
fi

if [ "$fail" -eq 0 ]; then
  echo "portability: src/core and src/hal are clean"
fi
exit "$fail"
