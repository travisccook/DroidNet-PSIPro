#!/usr/bin/env bash
# Part of the DroidNet Driveable-Animation Contract test harness — an additive layer
# bolted onto Neil Hutchison's PSI Pro firmware. Copyright (c) 2026 Travis Cook.
# MIT-licensed (see LICENSE-DroidNet-Contract). The PSI Pro firmware this layer
# attaches to is the work of Neil Hutchison and contributors, is NOT covered by that
# license, and carries no license of its own; see the NOTICE in README.md.
# SPDX-License-Identifier: MIT
#
# Host checks for the PSI Pro contract fork (no hardware / no AVR toolchain needed):
#   1. contract_core.h parser unit tests
#   2. ContractPSI.h firmware-layer type-check against a mock of the MaxPSI board API
set -e
cd "$(dirname "$0")"
echo "[1/2] contract_core parser unit tests"
clang++ -std=c++17 -Wall -Wextra -O0 test_contract_core.cpp -o /tmp/psi_contract_test
/tmp/psi_contract_test
echo "[2/2] ContractPSI.h firmware type-check"
clang++ -std=c++17 -Wall -Wextra compile_contract.cpp -o /tmp/psi_fw_syntax
/tmp/psi_fw_syntax
