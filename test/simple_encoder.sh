#!/bin/sh
##
##  Copyright (c) 2014 The WebM project authors. All Rights Reserved.
##
##  Use of this source code is governed by a BSD-style license
##  that can be found in the LICENSE file in the root of the source
##  tree. An additional intellectual property rights grant can be found
##  in the file PATENTS.  All contributing project authors may
##  be found in the AUTHORS file in the root of the source tree.
##
##  This file tests the libaom simple_encoder example. To add new tests to this
##  file, do the following:
##    1. Write a shell function (this is your test).
##    2. Add the function to simple_encoder_tests (on a new line).
##
. $(dirname $0)/tools_common.sh

# Environment check: $YUV_RAW_INPUT is required.
simple_encoder_verify_environment() {
  if [ ! -e "${YUV_RAW_INPUT}" ]; then
    echo "Libaom test data must exist in LIBVPX_TEST_DATA_PATH."
    return 1
  fi
}

# Runs simple_encoder using the codec specified by $1 with a frame limit of 100.
simple_encoder() {
  local encoder="${LIBAOM_BIN_PATH}/simple_encoder${AOM_TEST_EXE_SUFFIX}"
  local codec="$1"
  local output_file="${AOM_TEST_OUTPUT_DIR}/simple_encoder_${codec}.ivf"

  if [ ! -x "${encoder}" ]; then
    elog "${encoder} does not exist or is not executable."
    return 1
  fi

  eval "${AOM_TEST_PREFIX}" "${encoder}" "${codec}" "${YUV_RAW_INPUT_WIDTH}" \
      "${YUV_RAW_INPUT_HEIGHT}" "${YUV_RAW_INPUT}" "${output_file}" 9999 0 100 \
      ${devnull}

  [ -e "${output_file}" ] || return 1
}

simple_encoder_aom() {
  if [ "$(aom_encode_available)" = "yes" ]; then
    simple_encoder aom || return 1
  fi
}

# TODO(tomfinegan): Add a frame limit param to simple_encoder and enable this
# test. AV1 is just too slow right now: This test takes 4m30s+ on a fast
# machine.
DISABLED_simple_encoder_av1() {
  if [ "$(av1_encode_available)" = "yes" ]; then
    simple_encoder av1 || return 1
  fi
}

simple_encoder_tests="simple_encoder_aom
                      DISABLED_simple_encoder_av1"

run_tests simple_encoder_verify_environment "${simple_encoder_tests}"
