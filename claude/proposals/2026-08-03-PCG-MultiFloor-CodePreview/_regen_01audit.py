from __future__ import annotations

import difflib
from pathlib import Path


proposal_dir = Path(__file__).resolve().parent
repo_root = proposal_dir.parents[2]
stage_root = proposal_dir / "_stage01audit"
output_path = proposal_dir / "01-data-contract-core.patch"
relative_paths = [
    "Source/Demo/Public/PCG/ZeroEscapeGenerationTypes.h",
    "Source/Demo/Public/PCG/ZeroEscapeGenerationAssets.h",
    "Source/Demo/Private/PCG/ZeroEscapeGenerationAssets.cpp",
    "Source/Demo/Private/PCG/ZeroEscapeGenerationCore.h",
    "Source/Demo/Private/PCG/ZeroEscapeGenerationCore.cpp",
    "Source/Demo/Private/PCG/Tests/ZeroEscapeMultiFloorDataContractTests.cpp",
]

sections: list[str] = []
for relative_path in relative_paths:
    source_path = repo_root / relative_path
    staged_path = stage_root / relative_path
    old_lines = source_path.read_text(encoding="utf-8").splitlines() if source_path.exists() else []
    new_lines = staged_path.read_text(encoding="utf-8").splitlines()
    header = [f"diff --git a/{relative_path} b/{relative_path}"]
    if not source_path.exists():
        header.append("new file mode 100644")
    body = list(difflib.unified_diff(
        old_lines,
        new_lines,
        fromfile=f"a/{relative_path}" if source_path.exists() else "/dev/null",
        tofile=f"b/{relative_path}",
        lineterm="",
    ))
    sections.append("\n".join(header + body))

with output_path.open("w", encoding="utf-8", newline="\n") as output_file:
    output_file.write("\n".join(sections) + "\n")
