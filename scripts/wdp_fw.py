# DroidNet PSI Pro fork -- PlatformIO pre: extra script (platformio.ini: extra_scripts).
# Copyright (c) 2026 Travis Cook.  SPDX-License-Identifier: MIT
#
# Defines WDP_FW, the "fw" the board announces to the WCB (src/wdp/wdp_fork.h), as a C string:
#   `git rev-parse --short=7 HEAD`            the commit the firmware/ images are named after
#   + "-dirty"                                when `git status --porcelain` lists anything
#   "unknown"                                 when git fails (no git, not a checkout)
# env.StringifyMacro() does the quoting (PlatformIO docs, build_flags: Stringification). A pre:
# script appends to the global environment, so every env (PSIPro and PSIPro-i2c) gets it.
import subprocess

Import("env")  # noqa: F821 -- SCons builtin

PROJECT_DIR = env.subst("$PROJECT_DIR")  # noqa: F821


def git(*args):
    return subprocess.check_output(
        ["git", *args], cwd=PROJECT_DIR, stderr=subprocess.DEVNULL
    ).decode("utf-8").strip()


try:
    fw = git("rev-parse", "--short=7", "HEAD")
    if git("status", "--porcelain"):
        fw += "-dirty"
except (OSError, subprocess.CalledProcessError):
    fw = "unknown"

env.Append(CPPDEFINES=[("WDP_FW", env.StringifyMacro(fw))])  # noqa: F821
print("WDP_FW = %s" % fw)
