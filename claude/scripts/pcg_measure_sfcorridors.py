"""Read-only SFCorridors measurement pass for the runtime PCG vertical slice.

This script is executed by a temporary Unreal Editor process with the built-in
PythonScriptPlugin enabled from the command line. It never saves or modifies an
Unreal asset. The result is written under Saved/PCG for offline review.
"""

import json
import os
import traceback

import unreal


OUTPUT_NAME = "SFCorridorsMeasurement.json"
EXAMPLE_LEVELS = (
    "/Game/Assets/SFCorridors/Maps/SF_Corridors",
    "/Game/Assets/SFCorridors/Maps/Overview",
)
TARGET_MESHES = (
    "/Game/Assets/SFCorridors/Meshes/SM_Corridor_Segment",
    "/Game/Assets/SFCorridors/Meshes/SM_Room_Pass",
    "/Game/Assets/SFCorridors/Meshes/SM_Room_LeftTurn",
    "/Game/Assets/SFCorridors/Meshes/SM_Room_RightTurn",
    "/Game/Assets/SFCorridors/Meshes/SM_Room_T",
    "/Game/Assets/SFCorridors/Meshes/SM_door3_Wall",
    "/Game/Assets/SFCorridors/Meshes/SM_WallGlass",
    "/Game/Assets/SFCorridors/Meshes/SM_SideWalls",
)


def _vector_dict(value):
    return {"x": float(value.x), "y": float(value.y), "z": float(value.z)}


def _rotator_dict(value):
    return {
        "roll": float(value.roll),
        "pitch": float(value.pitch),
        "yaw": float(value.yaw),
    }


def _asset_path(asset):
    return unreal.EditorAssetLibrary.get_path_name_for_loaded_asset(asset).split(".", 1)[0]


def _read_mesh(mesh_path):
    mesh = unreal.load_asset(mesh_path)
    if not isinstance(mesh, unreal.StaticMesh):
        raise RuntimeError(f"Expected StaticMesh: {mesh_path}")

    bounds = mesh.get_bounding_box()
    sockets = []
    try:
        for socket in mesh.get_editor_property("sockets") or []:
            sockets.append(str(socket.get_editor_property("socket_name")))
    except Exception as exc:
        unreal.log_warning(f"[WARN] Socket inspection failed for {mesh_path}: {exc}")

    convex_count = None
    try:
        body_setup = mesh.get_editor_property("body_setup")
        if body_setup:
            aggregate = body_setup.get_editor_property("agg_geom")
            convex_count = len(aggregate.get_editor_property("convex_elems"))
    except Exception as exc:
        unreal.log_warning(f"[WARN] Collision inspection failed for {mesh_path}: {exc}")

    return {
        "asset_path": mesh_path,
        "bounds": {
            "min": _vector_dict(bounds.min),
            "max": _vector_dict(bounds.max),
            "center": _vector_dict((bounds.min + bounds.max) * 0.5),
            "size": _vector_dict(bounds.max - bounds.min),
        },
        "socket_names": sorted(sockets),
        "convex_collision_count": convex_count,
    }


def _read_current_level_actors(level_path):
    level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not level_subsystem.load_level(level_path):
        raise RuntimeError(f"Failed to load example level: {level_path}")

    records = []
    for actor in actor_subsystem.get_all_level_actors():
        component = actor.get_component_by_class(unreal.StaticMeshComponent)
        if not component:
            continue
        mesh = component.get_editor_property("static_mesh")
        if not mesh:
            continue
        mesh_path = _asset_path(mesh)
        if not mesh_path.startswith("/Game/Assets/SFCorridors/"):
            continue

        records.append(
            {
                "actor_label": actor.get_actor_label(),
                "actor_class": actor.get_class().get_name(),
                "mesh_path": mesh_path,
                "location": _vector_dict(actor.get_actor_location()),
                "rotation": _rotator_dict(actor.get_actor_rotation()),
                "scale": _vector_dict(actor.get_actor_scale3d()),
            }
        )

    records.sort(
        key=lambda item: (
            item["mesh_path"],
            item["location"]["x"],
            item["location"]["y"],
            item["location"]["z"],
            item["actor_label"],
        )
    )
    return records


def main():
    unreal.log("[PCG_MEASURE] Starting read-only SFCorridors measurement")
    missing = [path for path in TARGET_MESHES + EXAMPLE_LEVELS if not unreal.EditorAssetLibrary.does_asset_exist(path)]
    if missing:
        raise RuntimeError("Missing required assets: " + ", ".join(missing))

    result = {
        "schema_version": 1,
        "project_name": unreal.Paths.get_project_file_path(),
        "target_meshes": [_read_mesh(path) for path in TARGET_MESHES],
        "example_levels": {},
    }

    for level_path in EXAMPLE_LEVELS:
        actors = _read_current_level_actors(level_path)
        result["example_levels"][level_path] = {
            "sfc_static_mesh_actor_count": len(actors),
            "actors": actors,
        }
        unreal.log(f"[PCG_MEASURE] {level_path}: {len(actors)} SFC mesh actors")

    output_dir = os.path.abspath(os.path.join(unreal.Paths.project_saved_dir(), "PCG"))
    os.makedirs(output_dir, exist_ok=True)
    output_path = os.path.join(output_dir, OUTPUT_NAME)
    with open(output_path, "w", encoding="utf-8") as handle:
        json.dump(result, handle, ensure_ascii=False, indent=2, sort_keys=True)
        handle.write("\n")

    unreal.log(f"[PCG_MEASURE] Wrote {output_path}")
    unreal.log("[PCG_MEASURE] COMPLETE")


if __name__ == "__main__":
    try:
        main()
    except Exception:
        unreal.log_error("[PCG_MEASURE] FAILED\n" + traceback.format_exc())
        raise
