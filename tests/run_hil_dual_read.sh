#!/usr/bin/env bash
set -euo pipefail

serial_a="${1:-${SDRPLAY_SERIAL_A:-}}"
serial_b="${2:-${SDRPLAY_SERIAL_B:-}}"

if [[ -z "${serial_a}" || -z "${serial_b}" ]]; then
  echo "Usage: $0 SERIAL_A SERIAL_B"
  echo "Or set SDRPLAY_SERIAL_A and SDRPLAY_SERIAL_B environment variables."
  exit 2
fi

rate="${SDRPLAY_SAMPLE_RATE:-2000000}"
freq="${SDRPLAY_CENTER_FREQ:-100000000}"
duration="${SDRPLAY_DURATION_SEC:-10}"
num_elems="${SDRPLAY_NUM_ELEMS:-4096}"
timeout_us="${SDRPLAY_TIMEOUT_US:-100000}"
max_timeouts="${SDRPLAY_MAX_TIMEOUTS:-20}"
format="${SDRPLAY_FORMAT:-CS16}"
build_dir="${BUILD_DIR:-build}"

if [[ ! -x "${build_dir}/sdrplay_hil_dual_read" ]]; then
  cmake -S . -B "${build_dir}" -DENABLE_HIL_TESTS=ON
  cmake --build "${build_dir}" --target sdrplay_hil_dual_read
fi

"${build_dir}/sdrplay_hil_dual_read" \
  --serial-a "${serial_a}" \
  --serial-b "${serial_b}" \
  --rate "${rate}" \
  --freq "${freq}" \
  --duration "${duration}" \
  --num-elems "${num_elems}" \
  --timeout-us "${timeout_us}" \
  --max-timeouts "${max_timeouts}" \
  --format "${format}"
