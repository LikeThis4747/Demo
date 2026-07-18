#!/usr/bin/env python3
"""
UE5 Development Tools - Path and Discovery Utilities

All functions use filesystem-based discovery, no environment variable dependencies.
This is the canonical source for all path-related functions used across scripts.
"""

import os
from pathlib import Path
from typing import Optional


def find_ue5_project_root(start_dir: Optional[Path] = None) -> Optional[Path]:
    """
    Find UE5 project root by searching for .uproject file upward from start_dir.

    Args:
        start_dir: Starting directory for search (defaults to cwd)

    Returns:
        Path to project root directory containing .uproject file, or None if not found
    """
    if start_dir is None:
        start_dir = Path.cwd()

    current = Path(start_dir).resolve()

    while True:
        try:
            uprojects = list(current.glob("*.uproject"))
            if uprojects:
                return current
        except OSError:
            pass

        parent = current.parent
        if parent == current:
            return None
        current = parent


def find_project_name(start_dir: Optional[Path] = None) -> Optional[str]:
    """
    Find UE5 project name by searching for .uproject file.

    Args:
        start_dir: Starting directory for search (defaults to cwd)

    Returns:
        Project name (without .uproject extension) or None if not found or ambiguous
    """
    if start_dir is None:
        start_dir = Path.cwd()

    # Search in start_dir and up to 5 parent levels
    current = Path(start_dir).resolve()
    for _ in range(6):
        uprojects = list(current.glob("*.uproject"))
        if len(uprojects) == 1:
            return uprojects[0].stem
        elif len(uprojects) > 1:
            return None  # Ambiguous

        parent = current.parent
        if parent == current:
            break
        current = parent

    return None


def _get_ue5_search_roots() -> list[Path]:
    """Get common UE5 installation search roots."""
    program_files = os.environ.get("ProgramFiles", "C:\\Program Files")
    return [
        Path(program_files) / "Epic Games",
        Path("D:\\Epic Games"),
        Path("E:\\Epic Games"),
        Path("C:\\Epic Games"),
    ]


def _find_ue5_installations() -> list[Path]:
    """Find all UE5 installation directories, sorted by version (newest first)."""
    search_roots = _get_ue5_search_roots()
    installations = []

    for base_path in search_roots:
        if not base_path.exists():
            continue

        try:
            for item in base_path.iterdir():
                if item.is_dir() and item.name.startswith("UE_5"):
                    installations.append(item)
        except OSError:
            pass

    installations.sort(key=lambda p: p.name, reverse=True)
    return installations


def find_ue5_editor() -> Optional[Path]:
    """
    Find the latest installed UE5 Editor executable.

    Returns:
        Path to UnrealEditor.exe or None if not found
    """
    for install_dir in _find_ue5_installations():
        editor_path = install_dir / "Engine" / "Binaries" / "Win64" / "UnrealEditor.exe"
        if editor_path.exists():
            return editor_path
    return None


def find_runuat() -> Optional[Path]:
    """
    Find the RunUAT build tool from the latest installed UE5.

    Returns:
        Path to RunUAT.bat (Windows) or RunUAT.sh (Unix) or None if not found
    """
    import platform

    script_name = "RunUAT.bat" if platform.system() == "Windows" else "RunUAT.sh"

    for install_dir in _find_ue5_installations():
        runuat_path = install_dir / "Engine" / "Build" / "BatchFiles" / script_name
        if runuat_path.exists():
            return runuat_path
    return None


def find_build_bat() -> Optional[Path]:
    """
    Find the Build.bat script from the latest installed UE5.

    Returns:
        Path to Build.bat (Windows) or None if not found
    """
    import platform

    if platform.system() != "Windows":
        return None

    for install_dir in _find_ue5_installations():
        build_path = install_dir / "Engine" / "Build" / "BatchFiles" / "Build.bat"
        if build_path.exists():
            return build_path
    return None


def build_project(
    project_path: Path,
    config: str = "Development",
    platform: str = "Win64",
    timeout: int = 300,
) -> tuple[bool, str]:
    """
    Build UE5 project using Build.bat (UnrealBuildTool).

    This performs a quick editor build suitable for launching the editor
    with up-to-date binaries. Much faster than BuildCookRun.

    Args:
        project_path: Path to .uproject file
        config: Build configuration (Development, DebugGame, etc.)
        platform: Target platform (Win64, Mac, Linux)
        timeout: Build timeout in seconds (default 300 = 5 minutes)

    Returns:
        Tuple of (success: bool, message: str)
    """
    import subprocess

    build_bat = find_build_bat()
    if not build_bat:
        return False, "Could not find Build.bat tool"

    if not project_path.exists():
        return False, f"Project file not found: {project_path}"

    # Get project name from .uproject file
    project_name = project_path.stem

    # Build command using Build.bat for editor target
    # Format: Build.bat <TargetName> <Platform> <Configuration> -Project=<path> [options]
    target_name = f"{project_name}Editor"
    cmd = [
        str(build_bat),
        target_name,
        platform,
        config,
        f"-Project={project_path}",
        "-WaitMutex",
        "-FromMsBuild",
    ]

    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=timeout,
            cwd=project_path.parent,
        )

        if result.returncode == 0:
            return True, "Build completed successfully"
        else:
            # Extract relevant error info from output
            error_lines = []
            for line in result.stdout.split("\n") + result.stderr.split("\n"):
                if "error" in line.lower() or "failed" in line.lower():
                    error_lines.append(line.strip())

            error_msg = (
                "\n".join(error_lines[:10]) if error_lines else result.stderr[-500:]
            )
            return False, f"Build failed (exit code {result.returncode}):\n{error_msg}"

    except subprocess.TimeoutExpired:
        return False, f"Build timed out after {timeout} seconds"
    except Exception as e:
        return False, f"Build error: {e}"


def _get_latest_build_artifact_time(
    project_root: Path, project_name: str
) -> Optional[float]:
    """
    Get the modification time of the newest build artifact.

    Looks for editor binaries in Binaries/Win64/:
    - {ProjectName}Editor.exe
    - UnrealEditor-{ProjectName}.dll

    Args:
        project_root: Root directory of the project
        project_name: Name of the project (without .uproject extension)

    Returns:
        Modification timestamp of the newest artifact, or None if none exist
    """
    binaries_dir = project_root / "Binaries" / "Win64"
    if not binaries_dir.exists():
        return None

    artifact_patterns = [
        f"{project_name}Editor.exe",
        f"UnrealEditor-{project_name}.dll",
    ]

    latest_time = None
    for pattern in artifact_patterns:
        artifact_path = binaries_dir / pattern
        if artifact_path.exists():
            mtime = artifact_path.stat().st_mtime
            if latest_time is None or mtime > latest_time:
                latest_time = mtime

    return latest_time


def _get_latest_source_modification_time(
    project_root: Path, max_files: int = 10000
) -> Optional[float]:
    """
    Get the modification time of the newest source file that would require a rebuild.

    Scans:
    - Source/**/*.cpp and Source/**/*.h (C++ source)
    - *.uproject (project file)
    - Config/DefaultEngine.ini (engine config)
    - Source/**/*.Build.cs (module build configs)

    Args:
        project_root: Root directory of the project
        max_files: Maximum number of files to check (performance limit)

    Returns:
        Modification timestamp of the newest source file, or None if none found
    """
    latest_time = None
    files_checked = 0

    def update_latest(path: Path) -> bool:
        nonlocal latest_time, files_checked
        if files_checked >= max_files:
            return False
        try:
            if path.exists():
                mtime = path.stat().st_mtime
                if latest_time is None or mtime > latest_time:
                    latest_time = mtime
                files_checked += 1
        except OSError:
            pass
        return True

    # Check .uproject file
    for uproject in project_root.glob("*.uproject"):
        if not update_latest(uproject):
            break

    # Check DefaultEngine.ini
    engine_ini = project_root / "Config" / "DefaultEngine.ini"
    update_latest(engine_ini)

    # Check Source directory if it exists
    source_dir = project_root / "Source"
    if source_dir.exists():
        # Check C++ source files
        for pattern in ["**/*.cpp", "**/*.h", "**/*.Build.cs"]:
            try:
                for source_file in source_dir.glob(pattern):
                    if not update_latest(source_file):
                        break
            except OSError:
                pass
            if files_checked >= max_files:
                break

    return latest_time


def needs_rebuild(project_path: Path) -> tuple[bool, str]:
    """
    Check if a UE5 project needs rebuilding.

    Compares build artifact modification times against source file modification times
    to determine if a rebuild is required.

    Args:
        project_path: Path to .uproject file

    Returns:
        Tuple of (needs_rebuild: bool, reason: str)
        - (True, reason) if rebuild is needed
        - (False, reason) if build is up-to-date
    """
    if not project_path.exists():
        return True, f"Project file not found: {project_path}"

    project_root = project_path.parent
    project_name = project_path.stem

    # Get build artifact time
    artifact_time = _get_latest_build_artifact_time(project_root, project_name)
    if artifact_time is None:
        return True, "No build artifacts found (first build)"

    # Get source modification time
    source_time = _get_latest_source_modification_time(project_root)
    if source_time is None:
        # No source files found - unusual but possible for Blueprint-only projects
        return False, "Build is up-to-date (no source files found)"

    # Compare times
    if source_time > artifact_time:
        return True, "Source files modified after last build"

    return False, "Build is up-to-date"


def find_skills_root(start_path: Optional[Path] = None) -> Optional[Path]:
    """
    Find the skills root directory by searching upward for a directory
    containing known skill subdirectories.

    Args:
        start_path: Starting path for search (defaults to this file's location)

    Returns:
        Path to the skills root directory, or None if not found
    """
    if start_path is None:
        start_path = Path(__file__).resolve().parent

    current = Path(start_path).resolve()

    for _ in range(10):
        # Look for known skill directories as markers
        if (current / "unreal-python").is_dir():
            return current
        # Also check if we're inside .github/skills or similar structure
        if current.name == "skills" and (current / "unreal-python").is_dir():
            return current

        parent = current.parent
        if parent == current:
            break
        current = parent

    return None


def find_skill_path(
    skill_name: str, start_path: Optional[Path] = None
) -> Optional[Path]:
    """
    Find the path to a specific skill directory.

    Args:
        skill_name: Name of the skill (e.g., 'unreal-python')
        start_path: Starting path for search

    Returns:
        Path to the skill directory, or None if not found
    """
    skills_root = find_skills_root(start_path)
    if skills_root is None:
        return None

    skill_path = skills_root / skill_name
    if skill_path.exists() and skill_path.is_dir():
        return skill_path

    return None
