# 一次性脚本：把已插入的 FootstepNotify 子对象音量改为 5 倍（C++ 默认值只影响新实例）。
import unreal

REPORT = r"D:\UE5projects\Demo\claude\tmp_fix_footstep_volume_report.txt"
ANIMS = [
    "/Game/Sci_Fi_Character_08/Demo/Animations/ThirdPersonRun",
    "/Game/Sci_Fi_Character_08/Demo/Animations/ThirdPersonWalk",
]
NEW_VOLUME = 5.0

lines = []

for anim_path in ANIMS:
    anim = unreal.EditorAssetLibrary.load_asset(anim_path)
    if not anim:
        lines.append("%s NOT FOUND" % anim_path)
        continue
    fixed = 0
    for idx in range(8):
        sub_path = "%s.%s:ZeroEscapeFootstepNotify_%d" % (anim_path, anim.get_name(), idx)
        try:
            obj = unreal.load_object(None, sub_path)
        except Exception:
            obj = None
        if not obj:
            break
        try:
            obj.set_editor_property("volume_multiplier", NEW_VOLUME)
            fixed += 1
            lines.append("  %s -> volume=5.0" % obj.get_name())
        except Exception as e:
            lines.append("  set failed on %s: %s" % (obj.get_name(), e))
    if fixed > 0:
        anim.modify()
        unreal.EditorAssetLibrary.save_loaded_asset(anim)
    lines.append("%s fixed=%d" % (anim.get_name(), fixed))

with open(REPORT, "w", encoding="utf-8") as f:
    f.write("\n".join(lines))
