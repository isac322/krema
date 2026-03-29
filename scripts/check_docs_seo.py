#!/usr/bin/env python3
"""
check_docs_seo.py — Automated documentation-SEO consistency checker for Krema.

Validates that key documentation files contain expected SEO keywords and
positioning language from .claude/positioning.yml.

Exit code:
    0  — all checks passed
    1  — one or more checks failed
"""

from __future__ import annotations

import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).parent.parent

# Files and their required SEO terms (case-insensitive match)
REQUIRED_TERMS: list[tuple[str, list[str]]] = [
    (
        "README.md",
        [
            "KDE Plasma 6",
            "dock",
            "Wayland",
            "Latte Dock",
            "spiritual successor",
            "parabolic zoom",
        ],
    ),
    (
        "CLAUDE.md",
        [
            "kde plasma dock",
            "latte dock alternative",
        ],
    ),
    (
        "src/com.bhyoo.krema.metainfo.xml",
        [
            "dock",
            "kde",
            "plasma",
            "wayland",
            "launcher",
        ],
    ),
    (
        ".claude/agents/docs-seo.md",
        [
            "spiritual successor",
            "KDE Plasma 6",
            "positioning.yml",
        ],
    ),
]

# Phrases that must NOT appear (Latte Dock naming violations)
FORBIDDEN_PHRASES: list[tuple[str, list[str]]] = [
    (
        "README.md",
        [
            "latte dock replacement",
            "latte dock fork",
            "latte dock clone",
        ],
    ),
    (
        "src/com.bhyoo.krema.metainfo.xml",
        [
            "replacement for latte",
            "replaces latte",
        ],
    ),
]


@dataclass
class CheckResult:
    file: str
    missing: list[str] = field(default_factory=list)
    forbidden: list[str] = field(default_factory=list)

    @property
    def ok(self) -> bool:
        return not self.missing and not self.forbidden


def check_file(rel_path: str, required: list[str]) -> CheckResult:
    path = ROOT / rel_path
    result = CheckResult(file=rel_path)

    if not path.exists():
        result.missing = [f"<file not found: {rel_path}>"]
        return result

    content = path.read_text(encoding="utf-8").lower()
    for term in required:
        if term.lower() not in content:
            result.missing.append(term)

    return result


def check_forbidden(rel_path: str, forbidden: list[str]) -> CheckResult:
    path = ROOT / rel_path
    result = CheckResult(file=f"{rel_path} [forbidden]")

    if not path.exists():
        return result  # OK — file doesn't exist

    content = path.read_text(encoding="utf-8").lower()
    for phrase in forbidden:
        if phrase.lower() in content:
            result.forbidden.append(phrase)

    return result


def check_metainfo_keywords() -> CheckResult:
    """Check metainfo.xml <keywords> against positioning.yml expected list."""
    path = ROOT / "src/com.bhyoo.krema.metainfo.xml"
    result = CheckResult(file="metainfo.xml [keywords]")

    if not path.exists():
        result.missing = ["<metainfo.xml not found>"]
        return result

    content = path.read_text(encoding="utf-8").lower()

    expected = ["dock", "launcher", "kde", "plasma", "wayland", "zoom"]
    for kw in expected:
        if f"<keyword>{kw}</keyword>" not in content:
            result.missing.append(f"keyword: {kw}")

    return result


def main() -> int:
    results: list[CheckResult] = []

    for rel_path, required in REQUIRED_TERMS:
        results.append(check_file(rel_path, required))

    for rel_path, forbidden in FORBIDDEN_PHRASES:
        results.append(check_forbidden(rel_path, forbidden))

    results.append(check_metainfo_keywords())

    failed = [r for r in results if not r.ok]

    if not failed:
        print("✅  All documentation SEO checks passed.")
        return 0

    print("❌  Documentation SEO issues detected:\n")
    for r in failed:
        print(f"  {r.file}:")
        for term in r.missing:
            print(f"    - missing: {term!r}")
        for phrase in r.forbidden:
            print(f"    - FORBIDDEN: {phrase!r} (use 'spiritual successor' instead)")

    print(
        "\nRun `/check-docs-seo` or the @docs-seo agent in Claude Code to review and update."
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
