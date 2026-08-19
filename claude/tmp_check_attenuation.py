# 只读检查：SA_PursuerFootstep 衰减参数是否写进去。
import unreal

REPORT = r"D:\UE5projects\Demo\claude\tmp_attenuation_report.txt"
att = unreal.EditorAssetLibrary.load_asset("/Game/Sounds/SA_PursuerFootstep")
lines = []
if not att:
    lines.append("attenuation asset NOT FOUND")
else:
    try:
        s = att.get_editor_property("attenuation")
        for name in ("attenuate", "spatialize", "distance_algorithm", "attenuation_shape",
                     "attenuation_shape_extents", "falloff_distance", "radius", "db_attenuation_at_max"):
            try:
                lines.append("%s = %s" % (name, s.get_editor_property(name)))
            except Exception:
                pass
    except Exception as e:
        lines.append("read failed: %s" % e)
with open(REPORT, "w", encoding="utf-8") as f:
    f.write("\n".join(lines))
