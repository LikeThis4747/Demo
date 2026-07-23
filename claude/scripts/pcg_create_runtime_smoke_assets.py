"""Author the first project-owned runtime PCG smoke-test asset set.

The script is intentionally an editor-only, one-shot authoring helper. Runtime
generation remains entirely in the Demo module: these assets only provide the
Profile, logical Module Catalog, replaceable SFCorridors Presentation binding,
and an isolated PIE map.

Safety rules:
* Abort before writing if any target DataAsset/map already exists.
* Never modify or save anything below /Game/Assets/SFCorridors.
* Reuse the already-reviewed Generator Blueprint and verify its native parent.
* Save only the four task assets and the existing Generator Blueprint.
* Keep partial assets on failure for diagnosis; never auto-delete user content.
"""

import json
import os
import traceback

import unreal


PROFILE_PATH = "/Game/ZeroEscape/Generation/Data/DA_LevelGenerationProfile"
CATALOG_PATH = "/Game/ZeroEscape/Generation/Data/DA_LevelModuleCatalog"
PRESENTATION_PATH = "/Game/ZeroEscape/Generation/Presentation/DA_Presentation_SFCorridors"
GENERATOR_BP_PATH = "/Game/ZeroEscape/Generation/BP_ZeroEscapeRuntimeLevelGenerator"
MAP_PATH = "/Game/ZeroEscape/Generation/Maps/L_PCG_RuntimeTest"
MANIFEST_NAME = "RuntimeSmokeAssets.json"

# The SFC example map places Room_Pass instances approximately 658-662 cm apart.
# 660 cm is therefore the authored portal/grid step; mesh bounds are deliberately
# not used as CellSize because decorative walls protrude beyond that snap grid.
CELL_XY_CM = 660.0
CELL_Z_CM = 500.0
PORTAL_HALF_STEP_CM = CELL_XY_CM * 0.5
PORTAL_CENTER_Z_CM = 200.0

MESH_ROOM_PASS = "/Game/Assets/SFCorridors/Meshes/SM_Room_Pass"
MESH_ROOM_LEFT = "/Game/Assets/SFCorridors/Meshes/SM_Room_LeftTurn"
MESH_ROOM_RIGHT = "/Game/Assets/SFCorridors/Meshes/SM_Room_RightTurn"
MESH_CAP_WALL = "/Game/Assets/SFCorridors/Meshes/SM_WallGlass"


def _enum(enum_type, semantic_name):
    """Resolve UE Python enum spellings without coupling to generated case style."""
    normalized = semantic_name.replace("_", "").lower()
    for attribute in dir(enum_type):
        if attribute.startswith("_"):
            continue
        if attribute.replace("_", "").lower() == normalized:
            return getattr(enum_type, attribute)
    raise RuntimeError(f"Enum value not found: {enum_type}.{semantic_name}")


def _set(value, property_name, property_value):
    value.set_editor_property(property_name, property_value)
    return value


def _int_vector(x=0, y=0, z=0):
    return unreal.IntVector(int(x), int(y), int(z))


def _transform(x=0.0, y=0.0, z=0.0, yaw=0.0):
    # Unreal Python Rotator uses roll, pitch, yaw constructor semantics. Keyword
    # arguments prevent a silent axis swap if this helper is edited later.
    return unreal.Transform(
        location=unreal.Vector(float(x), float(y), float(z)),
        rotation=unreal.Rotator(roll=0.0, pitch=0.0, yaw=float(yaw)),
        scale=unreal.Vector(1.0, 1.0, 1.0),
    )


def _box(minimum, maximum):
    # FBox's Python constructor derives IsValid from the supplied Min/Max pair;
    # unlike reflected USTRUCTs it does not expose a third is_valid argument.
    return unreal.Box(unreal.Vector(*minimum), unreal.Vector(*maximum))


def _direction_contracts():
    direction_type = unreal.ZeroEscapeCardinalDirection
    return {
        "North": (_enum(direction_type, "North"), (0.0, PORTAL_HALF_STEP_CM, PORTAL_CENTER_Z_CM), 90.0),
        "East": (_enum(direction_type, "East"), (PORTAL_HALF_STEP_CM, 0.0, PORTAL_CENTER_Z_CM), 0.0),
        "South": (_enum(direction_type, "South"), (0.0, -PORTAL_HALF_STEP_CM, PORTAL_CENTER_Z_CM), -90.0),
        "West": (_enum(direction_type, "West"), (-PORTAL_HALF_STEP_CM, 0.0, PORTAL_CENTER_Z_CM), 180.0),
    }


def _make_portal(stable_socket_id, direction_name, policy_name="Required", closure_module_id=-1):
    direction, location, yaw = _direction_contracts()[direction_name]
    portal = unreal.ZeroEscapeModulePortal()
    _set(portal, "stable_socket_id", int(stable_socket_id))
    _set(portal, "cell_offset", _int_vector())
    _set(portal, "local_transform", _transform(*location, yaw=yaw))
    _set(portal, "direction", direction)
    _set(portal, "connector_type_id", 1)
    _set(portal, "display_type", "Walk")
    _set(portal, "width_class", 1)
    _set(portal, "height_layer", 0)
    _set(portal, "policy", _enum(unreal.ZeroEscapeSocketPolicy, policy_name))
    _set(portal, "closure_module_id", int(closure_module_id))
    return portal


def _make_anchor(stable_anchor_id, anchor_type_name, location=(0.0, 0.0, 100.0)):
    anchor = unreal.ZeroEscapeModuleAnchor()
    _set(anchor, "stable_anchor_id", int(stable_anchor_id))
    _set(anchor, "type", _enum(unreal.ZeroEscapeGameplayAnchorType, anchor_type_name))
    _set(anchor, "local_transform", _transform(*location))
    return anchor


def _make_module(stable_module_id, display_name, policy_name, role_names, portals, anchors=None):
    module = unreal.ZeroEscapeModuleDefinition()
    _set(module, "stable_module_id", int(stable_module_id))
    _set(module, "display_name", display_name)
    _set(module, "layout_policy", _enum(unreal.ZeroEscapeLayoutPolicy, policy_name))
    _set(module, "allowed_roles", [_enum(unreal.ZeroEscapeTopologyRole, name) for name in role_names])
    _set(module, "footprint", _int_vector(1, 1, 1))
    _set(module, "allowed_quarter_turns_mask", 0xF)
    _set(module, "weight", 100)

    # Catalog bounds describe the logical 660 cm cell, not the asymmetric mesh
    # shell. Measured shell protrusion is declared per Presentation binding below.
    _set(
        module,
        "local_bounds",
        _box(
            (-PORTAL_HALF_STEP_CM, -PORTAL_HALF_STEP_CM, 0.0),
            (PORTAL_HALF_STEP_CM, PORTAL_HALF_STEP_CM, CELL_Z_CM),
        ),
    )
    _set(module, "portals", list(portals))
    _set(module, "gameplay_anchors", list(anchors or []))
    return module


def _build_profile_values():
    shared = unreal.ZeroEscapeSharedRouteConstraints()
    _set(shared, "grid_extent_cells", unreal.IntPoint(8, 8))
    _set(shared, "critical_path_node_count", 4)
    _set(shared, "max_leaf_one_way_edge_count", 0)
    _set(shared, "max_required_route_extra_edge_count", 0)
    _set(shared, "max_objective_candidate_count", 1)
    _set(shared, "max_progression_search_states", 65536)
    # UHT exposes the C++ AStar prefix as Python's a_star prefix.
    _set(shared, "a_star_straight_step_cost", 10)
    _set(shared, "a_star_turn_penalty", 3)

    difficulties = []
    for difficulty_name in ("Easy", "Normal", "Hard"):
        definition = unreal.ZeroEscapeDifficultyDefinition()
        _set(definition, "difficulty", _enum(unreal.ZeroEscapeDifficulty, difficulty_name))
        _set(definition, "short_leaf_branch_count", 0)
        _set(definition, "forward_rejoin_branch_count", 0)
        _set(definition, "objective_candidate_count", 0)
        _set(definition, "required_objective_count", 0)
        difficulties.append(definition)

    flow = unreal.ZeroEscapeFlowDefinition()
    _set(flow, "stable_flow_id", "EscapeOnly")
    _set(flow, "flow_version", 1)
    _set(flow, "completion_rule", _enum(unreal.ZeroEscapeCompletionRule, "EscapeOnly"))
    _set(flow, "allowed_objective_roles", [])

    budgets = unreal.ZeroEscapeSolverBudgets()
    # Values are written explicitly so the smoke asset remains reviewable even if
    # a later C++ constructor default changes. They stay below every code hard cap.
    explicit_budgets = {
        "max_layout_attempts": 3,
        "max_socket_backtracks": 128,
        "max_socket_candidate_checks": 4096,
        "max_a_star_expanded_states": 50000,
        "max_a_star_route_attempts": 128,
        "max_wfc_backtracks": 128,
        "max_wfc_observation_count": 256,
        "max_wfc_support_updates": 250000,
        "max_wfc_active_cells": 256,
        "max_wfc_variants": 64,
        "max_wfc_snapshot_memory_mb": 16,
        "max_wfc_cumulative_snapshot_copy_mb": 64,
        "max_total_work_units": 750000,
    }
    for property_name, property_value in explicit_budgets.items():
        _set(budgets, property_name, property_value)

    return shared, difficulties, [flow], budgets


def _build_catalog_modules():
    # Cap is a logical one-portal module. Its presentation mesh is translated to
    # that portal plane, so the closure occupies the doorway rather than the middle
    # of the logical cap cell.
    cap = _make_module(
        100,
        "Cap_WallGlass",
        "Cap",
        [],
        [_make_portal(0, "South")],
    )

    # Start/Exit reuse Room_Pass as semantic wrappers. One opening is consumed by
    # the critical route; the other is Sealable and receives the measured wall cap.
    start = _make_module(
        200,
        "Socket_Start_Pass",
        "SocketModule",
        ["Start"],
        [
            _make_portal(0, "North"),
            _make_portal(1, "South", "Sealable", 100),
        ],
        [_make_anchor(0, "PlayerSpawn", (0.0, 0.0, 100.0))],
    )
    exit_module = _make_module(
        201,
        "Socket_Exit_Pass",
        "SocketModule",
        ["Exit"],
        [
            _make_portal(0, "South"),
            _make_portal(1, "North", "Sealable", 100),
        ],
        [_make_anchor(0, "Exit", (0.0, 0.0, 100.0))],
    )

    # The three WFC tiles cover straight and both handed corner presentations.
    # Left/Right have equivalent rotated connector domains but remain distinct
    # variants, so future art differences do not require changing the solver.
    straight = _make_module(
        300,
        "WFC_Pass",
        "WfcSingleCell",
        ["MainPath"],
        [_make_portal(0, "North"), _make_portal(1, "South")],
    )
    left = _make_module(
        301,
        "WFC_LeftTurn",
        "WfcSingleCell",
        ["MainPath"],
        [_make_portal(0, "East"), _make_portal(1, "South")],
    )
    right = _make_module(
        302,
        "WFC_RightTurn",
        "WfcSingleCell",
        ["MainPath"],
        [_make_portal(0, "West"), _make_portal(1, "South")],
    )
    return [cap, start, exit_module, straight, left, right]


def _make_presentation_binding(stable_module_id, mesh_path, overhang_cm, pivot=None):
    mesh = unreal.load_asset(mesh_path)
    if not isinstance(mesh, unreal.StaticMesh):
        raise RuntimeError(f"Expected StaticMesh: {mesh_path}")
    binding = unreal.ZeroEscapePresentationBinding()
    _set(binding, "stable_module_id", int(stable_module_id))
    _set(binding, "spawn_policy", _enum(unreal.ZeroEscapePresentationSpawnPolicy, "InstancedStaticMesh"))
    _set(binding, "static_mesh", mesh)
    _set(binding, "pivot_correction", pivot or _transform())
    _set(binding, "bounds_overhang_allowance_cm", float(overhang_cm))
    _set(binding, "collision_profile_name", "BlockAll")
    _set(binding, "can_ever_affect_navigation", False)
    return binding


def _build_presentation_bindings():
    # Allowances are the smallest rounded values covering the measured bounds.
    # The C++ contract adds only a 2 cm numeric tolerance and rejects >100 cm.
    room_pass = 66.0
    return [
        _make_presentation_binding(
            100,
            MESH_CAP_WALL,
            24.0,
            _transform(y=-PORTAL_HALF_STEP_CM),
        ),
        _make_presentation_binding(200, MESH_ROOM_PASS, room_pass),
        _make_presentation_binding(201, MESH_ROOM_PASS, room_pass),
        _make_presentation_binding(300, MESH_ROOM_PASS, room_pass),
        _make_presentation_binding(301, MESH_ROOM_LEFT, 55.0),
        _make_presentation_binding(302, MESH_ROOM_RIGHT, 46.0),
    ]


def _load_native_class(script_name):
    native_class = unreal.load_class(None, f"/Script/Demo.{script_name}")
    if not native_class:
        raise RuntimeError(f"Native class unavailable: /Script/Demo.{script_name}")
    return native_class


def _create_data_asset(asset_path, native_class):
    package_path, asset_name = asset_path.rsplit("/", 1)
    factory = unreal.DataAssetFactory()
    _set(factory, "data_asset_class", native_class)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        asset_name,
        package_path,
        native_class,
        factory,
    )
    if not asset:
        raise RuntimeError(f"Failed to create DataAsset: {asset_path}")
    return asset


def _preflight_and_build_transient_values():
    # Construct every reflected enum/struct before creating packages. A Python API
    # spelling or UHT exposure error therefore fails with zero project asset writes.
    targets = (PROFILE_PATH, CATALOG_PATH, PRESENTATION_PATH, MAP_PATH)
    existing = [path for path in targets if unreal.EditorAssetLibrary.does_asset_exist(path)]
    if existing:
        raise RuntimeError("Target assets already exist; refusing overwrite: " + ", ".join(existing))
    for path in (GENERATOR_BP_PATH, MESH_ROOM_PASS, MESH_ROOM_LEFT, MESH_ROOM_RIGHT, MESH_CAP_WALL):
        if not unreal.EditorAssetLibrary.does_asset_exist(path):
            raise RuntimeError(f"Required source asset missing: {path}")

    profile_values = _build_profile_values()
    catalog_modules = _build_catalog_modules()
    presentation_bindings = _build_presentation_bindings()
    return profile_values, catalog_modules, presentation_bindings


def _configure_generator_blueprint(profile, catalog, presentation):
    blueprint = unreal.load_asset(GENERATOR_BP_PATH)
    if not isinstance(blueprint, unreal.Blueprint):
        raise RuntimeError(f"Generator Blueprint missing or wrong class: {GENERATOR_BP_PATH}")
    generated_class = blueprint.generated_class()
    cdo = unreal.get_default_object(generated_class)
    # BlueprintGeneratedClass does not expose GetSuperClass to Python in this UE
    # build. The CDO's reflected Python type still performs the exact is-a check
    # needed here and accepts only this native generator or one of its subclasses.
    if not isinstance(cdo, unreal.ZeroEscapeRuntimeLevelGenerator):
        raise RuntimeError(f"Unexpected Generator Blueprint class: {generated_class.get_path_name()}")
    request = unreal.ZeroEscapeGenerationRequest()
    _set(request, "seed", 12345)
    _set(request, "difficulty", _enum(unreal.ZeroEscapeDifficulty, "Normal"))
    _set(request, "flow_profile_id", "EscapeOnly")
    _set(cdo, "trigger_mode", _enum(unreal.ZeroEscapeGenerationTrigger, "BeginPlay"))
    _set(cdo, "default_request", request)
    _set(cdo, "generation_profile", profile)
    _set(cdo, "module_catalog", catalog)
    _set(cdo, "presentation_profile", presentation)
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    return blueprint, generated_class


def _create_test_level(generator_class):
    level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not level_subsystem.new_level(MAP_PATH):
        raise RuntimeError(f"Failed to create level: {MAP_PATH}")

    generator = actor_subsystem.spawn_actor_from_class(generator_class, unreal.Vector(0.0, 0.0, 0.0))
    if not generator:
        raise RuntimeError("Failed to spawn runtime Generator Blueprint")
    generator.set_actor_label("RuntimeLevelGenerator")

    # The staging island is outside the positive 8x8 generation grid. PIE starts
    # safely here while the Generator performs its synchronous BeginPlay smoke run.
    floor = actor_subsystem.spawn_actor_from_class(
        unreal.StaticMeshActor,
        unreal.Vector(-1500.0, 0.0, -25.0),
    )
    floor.set_actor_label("StagingPlatform")
    floor.set_actor_scale3d(unreal.Vector(10.0, 10.0, 0.5))
    floor_component = floor.get_component_by_class(unreal.StaticMeshComponent)
    floor_component.set_editor_property("static_mesh", unreal.load_asset("/Engine/BasicShapes/Cube"))

    player_start = actor_subsystem.spawn_actor_from_class(
        unreal.PlayerStart,
        unreal.Vector(-1500.0, 0.0, 100.0),
    )
    player_start.set_actor_label("PlayerStart_Staging")

    sun = actor_subsystem.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0.0, 0.0, 1000.0))
    sun.set_actor_label("Sun")
    sun.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=-45.0, yaw=-30.0), False)
    skylight = actor_subsystem.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(-1500.0, 0.0, 500.0))
    skylight.set_actor_label("SkyLight")
    atmosphere = actor_subsystem.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0.0, 0.0, 0.0))
    atmosphere.set_actor_label("SkyAtmosphere")

    if not level_subsystem.save_current_level():
        raise RuntimeError(f"Failed to save level: {MAP_PATH}")
    return {
        "generator": generator.get_actor_label(),
        "staging_platform": floor.get_actor_label(),
        "player_start": player_start.get_actor_label(),
    }


def main():
    unreal.log("[PCG_AUTHOR] Starting runtime smoke asset authoring")
    profile_values, catalog_modules, presentation_bindings = _preflight_and_build_transient_values()
    profile_class = _load_native_class("ZeroEscapeLevelGenerationProfile")
    catalog_class = _load_native_class("ZeroEscapeModuleCatalog")
    presentation_class = _load_native_class("ZeroEscapePresentationProfile")

    with unreal.ScopedEditorTransaction("Author Runtime PCG Smoke Assets"):
        profile = _create_data_asset(PROFILE_PATH, profile_class)
        shared, difficulties, flows, budgets = profile_values
        _set(profile, "profile_version", 1)
        _set(profile, "shared_route_constraints", shared)
        _set(profile, "difficulties", difficulties)
        _set(profile, "flows", flows)
        _set(profile, "solver_budgets", budgets)
        _set(profile, "require_effective_wfc_choice", False)

        catalog = _create_data_asset(CATALOG_PATH, catalog_class)
        _set(catalog, "catalog_version", 1)
        _set(catalog, "cell_size", unreal.Vector(CELL_XY_CM, CELL_XY_CM, CELL_Z_CM))
        _set(catalog, "modules", catalog_modules)

        presentation = _create_data_asset(PRESENTATION_PATH, presentation_class)
        _set(presentation, "presentation_version", 1)
        _set(presentation, "bindings", presentation_bindings)

        blueprint, generator_class = _configure_generator_blueprint(profile, catalog, presentation)

        for asset_path in (PROFILE_PATH, CATALOG_PATH, PRESENTATION_PATH, GENERATOR_BP_PATH):
            if not unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=False):
                raise RuntimeError(f"Failed to save asset: {asset_path}")

        level_actors = _create_test_level(generator_class)

    # Read back the minimum facts that must survive serialization. Runtime semantic
    # validation still occurs through Generate() during the subsequent PIE pass.
    readback = {
        "profile_difficulty_count": len(profile.get_editor_property("difficulties")),
        "profile_flow_count": len(profile.get_editor_property("flows")),
        "catalog_module_count": len(catalog.get_editor_property("modules")),
        "presentation_binding_count": len(presentation.get_editor_property("bindings")),
        "generator_parent": "/Script/Demo.ZeroEscapeRuntimeLevelGenerator",
        "level_actors": level_actors,
    }
    if readback["profile_difficulty_count"] != 3 or readback["catalog_module_count"] != 6:
        raise RuntimeError(f"Serialized asset readback mismatch: {readback}")
    if readback["presentation_binding_count"] != readback["catalog_module_count"]:
        raise RuntimeError(f"Catalog/Presentation coverage mismatch: {readback}")

    output_dir = os.path.abspath(os.path.join(unreal.Paths.project_saved_dir(), "PCG"))
    os.makedirs(output_dir, exist_ok=True)
    manifest_path = os.path.join(output_dir, MANIFEST_NAME)
    with open(manifest_path, "w", encoding="utf-8") as handle:
        json.dump(readback, handle, ensure_ascii=False, indent=2, sort_keys=True)
        handle.write("\n")

    unreal.log(f"[PCG_AUTHOR] Wrote {manifest_path}")
    unreal.log("[PCG_AUTHOR] COMPLETE")


if __name__ == "__main__":
    try:
        main()
    except Exception:
        unreal.log_error("[PCG_AUTHOR] FAILED\n" + traceback.format_exc())
        raise
