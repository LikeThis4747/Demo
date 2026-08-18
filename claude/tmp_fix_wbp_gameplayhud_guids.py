import unreal

ASSET_PATH = "/Game/ZeroEscape/UI/WBP_GameplayHUD"
PROP_NAME = "widget_variable_name_to_guid_map"
STALE_NAMES = {"ExitLockedWarningRow", "ExitLockedWarningPrefixText"}

asset = unreal.EditorAssetLibrary.load_asset(ASSET_PATH)
if asset is None:
    raise RuntimeError(f"Failed to load asset: {ASSET_PATH}")

try:
    guid_map = asset.get_editor_property(PROP_NAME)
    unreal.log(f"GUID map before: type={type(guid_map)} value={guid_map}")
except Exception as exc:
    unreal.log_warning(f"Failed to read {PROP_NAME}: {exc}")
    guid_map = None

removed = []
if guid_map is not None:
    try:
        for key in list(guid_map.keys()):
            key_name = str(key)
            if key_name in STALE_NAMES:
                del guid_map[key]
                removed.append(key_name)
        unreal.log(f"GUID map after removal: {guid_map}")
    except Exception as exc:
        unreal.log_warning(f"Failed to remove stale keys directly: {exc}; clearing map for deterministic rebuild")
        try:
            guid_map.clear()
        except Exception as clear_exc:
            unreal.log_warning(f"Failed to clear map in-place: {clear_exc}; assigning empty map")
            asset.set_editor_property(PROP_NAME, {})
else:
    unreal.log_warning("GUID map unavailable; assigning empty map for deterministic rebuild")
    asset.set_editor_property(PROP_NAME, {})

if removed:
    unreal.log(f"Removed stale GUID entries: {removed}")
else:
    unreal.log("No stale entries removed by name; rebuild path was used if needed")

asset.modify()
saved = unreal.EditorAssetLibrary.save_asset(ASSET_PATH)
unreal.log(f"save_asset({ASSET_PATH}) -> {saved}")
if not saved:
    raise RuntimeError("Failed to save WBP_GameplayHUD")
