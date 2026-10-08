#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
#
# `krema <unknown option>` must exit non-zero with an "Unknown option" error
# instead of starting the dock (issue #59). Runs under run-with-kwin.sh: the
# check happens after QApplication has taken its own options, so it needs a
# display. The timeout ends a dock that ignored the option and started.
#
# Usage: cli-unknown-option.sh <krema-binary>

set -eu

out=$(timeout 30 "$1" --not-a-real-option 2>&1) && {
    echo "krema --not-a-real-option exited 0" >&2
    exit 1
}
case $out in
    *"Unknown option"*) ;;
    *)
        echo "$out" >&2
        exit 1
        ;;
esac
echo "$out"
