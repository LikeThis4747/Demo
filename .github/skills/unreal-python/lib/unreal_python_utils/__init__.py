#!/usr/bin/env python3
"""
UE5 Development Tools - Shared Utilities

This package provides common utilities shared across UE5 Python development scripts.
All functions use filesystem-based discovery.

Usage:
    import sys
    from pathlib import Path
    sys.path.insert(0, str(Path(__file__).parent.parent / "lib"))
    from unreal_python_utils import find_ue5_project_root, find_ue5_editor
"""

from .paths import (
    find_ue5_project_root,
    find_project_name,
    find_ue5_editor,
    find_runuat,
    find_build_bat,
    build_project,
    needs_rebuild,
    find_skills_root,
    find_skill_path,
)

__all__ = [
    "find_ue5_project_root",
    "find_project_name",
    "find_ue5_editor",
    "find_runuat",
    "find_build_bat",
    "build_project",
    "needs_rebuild",
    "find_skills_root",
    "find_skill_path",
]
