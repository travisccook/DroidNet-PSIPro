#!/usr/bin/env bash
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
