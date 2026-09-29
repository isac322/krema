#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Render a JUnit XML report as GitHub step-summary Markdown.

    junit-summary.py TITLE REPORT.xml... [--forbid-skip REGEX]

Prints Markdown to stdout. Several reports get one section each under the
title (e.g. one per shard). Exits 1 if a skip message matches REGEX in any
file (so a lost capability, e.g. KWin falling back to QPainter, cannot pass
silently). A missing report is reported, not an error: the test step
already failed.
"""

from __future__ import annotations

import argparse
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


def cell(text: str) -> str:
    text = " ".join(text.split())
    if len(text) > 300:
        text = text[:297] + "..."
    return text.replace("|", "\\|")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("title")
    ap.add_argument("reports", type=Path, nargs="+", metavar="report")
    ap.add_argument("--forbid-skip", type=re.compile)
    args = ap.parse_args()

    print(f"## {args.title}\n")
    forbidden: list[str] = []
    for report in args.reports:
        if len(args.reports) > 1:
            print(f"### `{report}`\n")
        forbidden += summarize(report, args.forbid_skip)
    if forbidden:
        print(
            f"Forbidden skips ({args.forbid_skip.pattern}): "
            + ", ".join(f"`{n}`" for n in forbidden)
            + "\n"
        )
        print(f"forbidden skips: {forbidden}", file=sys.stderr)
        return 1
    return 0


def summarize(report: Path, forbid_skip: re.Pattern[str] | None) -> list[str]:
    """Render one report file; return the names of forbidden skips."""
    if not report.is_file():
        print(f"No JUnit report at `{report}`.\n")
        return []

    root = ET.parse(report).getroot()
    counts = {"passed": 0, "failed": 0, "skipped": 0}
    problems: list[tuple[str, str, str]] = []
    forbidden = []
    for case in root.iter("testcase"):
        name = f"{case.get('classname', '')}::{case.get('name', '')}".strip(":")
        failure = case.find("failure")
        if failure is None:
            failure = case.find("error")
        skipped = case.find("skipped")
        if failure is not None:
            counts["failed"] += 1
            problems.append(("❌ failed", name, failure.get("message") or (failure.text or "")))
        elif skipped is not None:
            counts["skipped"] += 1
            message = skipped.get("message") or (skipped.text or "")
            problems.append(("⏭️ skipped", name, message))
            if forbid_skip and forbid_skip.search(message):
                forbidden.append(name)
        else:
            counts["passed"] += 1

    print(f"**{counts['passed']} passed, {counts['failed']} failed, {counts['skipped']} skipped**\n")
    if problems:
        print("| Result | Test | Message |\n|---|---|---|")
        for result, name, message in problems:
            print(f"| {result} | `{cell(name)}` | {cell(message)} |")
        print()
    return forbidden


if __name__ == "__main__":
    sys.exit(main())
