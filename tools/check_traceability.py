#!/usr/bin/env python3
"""Validate the Mini VCU requirements traceability matrix."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MATRIX = ROOT / "docs" / "requirements-traceability.json"
DEFAULT_SUMMARY = ROOT / "docs" / "13-requirements-traceability.md"
ID_PATTERN = re.compile(r"^(REQ|ADV|ACC)-[A-Z]+-[0-9]{3}$")
REFERENCE_GROUPS = ("source", "design", "implementation",
                    "verification", "evidence")


class TraceabilityError(ValueError):
    pass


def _validate_reference(requirement_id: str, group: str,
                        reference: object) -> None:
    if not isinstance(reference, dict):
        raise TraceabilityError(f"{requirement_id}/{group}: reference is not an object")
    relative = reference.get("path")
    marker = reference.get("contains")
    if not isinstance(relative, str) or not relative:
        raise TraceabilityError(f"{requirement_id}/{group}: path is missing")
    if not isinstance(marker, str) or not marker:
        raise TraceabilityError(f"{requirement_id}/{group}: contains marker is missing")
    path = Path(relative)
    if path.is_absolute() or ".." in path.parts:
        raise TraceabilityError(f"{requirement_id}/{group}: unsafe path {relative}")
    absolute = ROOT / path
    if not absolute.is_file():
        raise TraceabilityError(f"{requirement_id}/{group}: file not found {relative}")
    text = absolute.read_text(encoding="utf-8", errors="replace")
    if marker not in text:
        raise TraceabilityError(
            f"{requirement_id}/{group}: marker not found in {relative}: {marker!r}")


def validate_matrix(matrix_path: Path = DEFAULT_MATRIX,
                    summary_path: Path = DEFAULT_SUMMARY) -> dict[str, int]:
    data = json.loads(matrix_path.read_text(encoding="utf-8"))
    allowed = set(data.get("statuses", []))
    if allowed != {"verified", "partial", "open"}:
        raise TraceabilityError("status vocabulary must be verified/partial/open")
    requirements = data.get("requirements")
    if not isinstance(requirements, list) or not requirements:
        raise TraceabilityError("requirements list is empty")

    summary = summary_path.read_text(encoding="utf-8")
    seen: set[str] = set()
    counts = {status: 0 for status in allowed}
    for requirement in requirements:
        requirement_id = requirement.get("id")
        status = requirement.get("status")
        if not isinstance(requirement_id, str) or not ID_PATTERN.fullmatch(requirement_id):
            raise TraceabilityError(f"invalid requirement id: {requirement_id!r}")
        if requirement_id in seen:
            raise TraceabilityError(f"duplicate requirement id: {requirement_id}")
        seen.add(requirement_id)
        if status not in allowed:
            raise TraceabilityError(f"{requirement_id}: invalid status {status!r}")
        counts[status] += 1
        if not requirement.get("statement"):
            raise TraceabilityError(f"{requirement_id}: statement is empty")
        if f"| {requirement_id} |" not in summary:
            raise TraceabilityError(f"{requirement_id}: missing from Markdown summary")

        for group in REFERENCE_GROUPS:
            references = requirement.get(group)
            if not isinstance(references, list):
                raise TraceabilityError(f"{requirement_id}: {group} must be a list")
            for reference in references:
                _validate_reference(requirement_id, group, reference)

        if not requirement["source"] or not requirement["design"]:
            raise TraceabilityError(f"{requirement_id}: source/design trace is required")
        if status == "verified" and (not requirement["implementation"] or
                                     not requirement["verification"] or
                                     not requirement["evidence"]):
            raise TraceabilityError(
                f"{requirement_id}: verified requires implementation, verification and evidence")
        if status != "verified" and not requirement.get("gap"):
            raise TraceabilityError(f"{requirement_id}: non-verified item needs a gap")

    counts["total"] = len(requirements)
    return counts


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--matrix", type=Path, default=DEFAULT_MATRIX)
    parser.add_argument("--summary", type=Path, default=DEFAULT_SUMMARY)
    args = parser.parse_args()
    try:
        counts = validate_matrix(args.matrix, args.summary)
    except (OSError, json.JSONDecodeError, TraceabilityError) as exc:
        print(f"TRACEABILITY CHECK FAIL: {exc}", file=sys.stderr)
        return 1
    print("TRACEABILITY CHECK PASS: "
          f"total={counts['total']} verified={counts['verified']} "
          f"partial={counts['partial']} open={counts['open']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
