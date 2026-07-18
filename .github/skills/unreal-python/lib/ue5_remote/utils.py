#!/usr/bin/env python3
"""
UE5 Development Tools - Path Resolution Utilities

This module re-exports utilities from unreal_python_utils for backward compatibility.
"""

import sys
from pathlib import Path

# unreal_python_utils is a sibling package in the same lib/ directory
_lib_path = Path(__file__).resolve().parent.parent
if str(_lib_path) not in sys.path:
    sys.path.insert(0, str(_lib_path))

# Re-export all utilities from unreal_python_utils
from unreal_python_utils import (
    find_ue5_project_root,
    find_project_name,
    find_ue5_editor,
    find_skills_root,
    find_skill_path,
)

__all__ = [
    "find_ue5_project_root",
    "find_project_name",
    "find_ue5_editor",
    "find_skills_root",
    "find_skill_path",
]
