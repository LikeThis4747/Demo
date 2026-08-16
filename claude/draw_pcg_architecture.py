# -*- coding: utf-8 -*-
"""PCG 技术总览架构图：布局求解 → LevelPlan → 实例化 / 机关放置 → 可玩迷宫"""
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch

plt.rcParams["font.sans-serif"] = ["Microsoft YaHei"]
plt.rcParams["axes.unicode_minus"] = False

C_EDGE = "#3D4451"
C_TEXT = "#1F2329"
C_SOLVE = "#EAF2FB"   # 布局求解：淡蓝
C_POP = "#EAF6EC"     # 机关放置：淡绿
C_GRAY = "#F3F4F6"    # 实例化：中性灰
C_TECH1 = "#1F6FEB"   # 技术点一：蓝
C_TECH2 = "#C2410C"   # 技术点二：橙

fig, ax = plt.subplots(figsize=(16, 10), dpi=200)
ax.set_xlim(0, 16)
ax.set_ylim(0, 10)
ax.axis("off")


def rbox(x, y, w, h, fc, ec=C_EDGE, lw=1.6):
    ax.add_patch(FancyBboxPatch(
        (x, y), w, h, boxstyle="round,pad=0.02,rounding_size=0.12",
        facecolor=fc, edgecolor=ec, linewidth=lw))


def text(x, y, s, size=12, bold=False, color=C_TEXT, ha="center", va="center"):
    ax.text(x, y, s, fontsize=size, color=color, ha=ha, va=va,
            fontweight="bold" if bold else "normal")


def arrow(x1, y1, x2, y2, color=C_EDGE, lw=1.8):
    ax.add_patch(FancyArrowPatch(
        (x1, y1), (x2, y2), arrowstyle="-|>", mutation_scale=18,
        color=color, linewidth=lw, shrinkA=2, shrinkB=2))


# ── 顶部：输入 ─────────────────────────────────
rbox(4.3, 8.95, 4.4, 0.75, "#FFFFFF")
text(6.5, 9.33, "Seed + 难度 + 配置资产", size=13.5, bold=True)

arrow(6.5, 8.93, 6.5, 8.62)

# ── 布局求解大框 ───────────────────────────────
rbox(1.2, 5.85, 10.7, 2.75, C_SOLVE)
text(1.55, 8.28, "布局求解", size=14, bold=True, ha="left")

sub = [
    (1.65, "多层规划", "先放跨层结构", "再逐层填充"),
    (5.00, "单层求解", "路线草图引导", "多解择优"),
    (8.35, "WFC 内核", "每格16种开口组合", "连通/结构/数量约束"),
]
for x, name, l1, l2 in sub:
    rbox(x, 6.35, 3.05, 1.45, "#FFFFFF", lw=1.4)
    text(x + 1.525, 7.52, name, size=12.5, bold=True)
    text(x + 1.525, 7.02, l1, size=10.5)
    text(x + 1.525, 6.68, l2, size=10.5)
arrow(4.72, 7.08, 4.98, 7.08, lw=1.6)
arrow(8.07, 7.08, 8.33, 7.08, lw=1.6)

# 布局求解右侧技术点标注
text(12.15, 7.55, "跨层结构 + 逐层 WFC", size=12.5, bold=True, color=C_TECH1, ha="left")
text(12.15, 7.00, "硬约束守合法", size=12.5, bold=True, color=C_TECH2, ha="left")
text(12.15, 6.62, "软约束引路线", size=12.5, bold=True, color=C_TECH2, ha="left")

# ── LevelPlan 分叉 ─────────────────────────────
arrow(6.5, 5.83, 6.5, 5.28)
text(6.75, 5.56, "LevelPlan", size=11.5, color=C_TEXT, ha="left")
ax.plot([3.4, 9.6], [5.28, 5.28], color=C_EDGE, lw=1.8)
arrow(3.4, 5.28, 3.4, 4.82)
arrow(9.6, 5.28, 9.6, 4.82)

# ── 左：房间结构 HISM（实例化，灰） ─────────────
rbox(1.75, 3.5, 3.3, 1.3, C_GRAY)
text(3.4, 4.42, "房间结构 HISM", size=12.5, bold=True)
text(3.4, 3.95, "实例化绘制", size=10.5, color="#6B7280")

# ── 右：机关放置大框 ───────────────────────────
rbox(7.0, 3.3, 5.2, 1.5, C_POP)
text(7.3, 4.5, "机关放置", size=13, bold=True, ha="left")
text(9.6, 4.05, "重建通行图 → 图上评分", size=11)
text(9.6, 3.66, "Spawn 机关 / 资源 / 光团", size=11)
text(12.45, 4.15, "硬约束保可行", size=12, bold=True, color=C_TECH2, ha="left")
text(12.45, 3.78, "软约束调节奏", size=12, bold=True, color=C_TECH2, ha="left")

# ── 底部汇合 → 可玩迷宫 ────────────────────────
ax.plot([3.4, 3.4], [3.48, 2.62], color=C_EDGE, lw=1.8)
ax.plot([9.6, 9.6], [3.28, 2.62], color=C_EDGE, lw=1.8)
ax.plot([3.4, 9.6], [2.62, 2.62], color=C_EDGE, lw=1.8)
arrow(6.5, 2.62, 6.5, 2.12)

rbox(5.05, 1.35, 2.9, 0.75, "#FFFFFF", lw=1.8)
text(6.5, 1.73, "可玩迷宫", size=14, bold=True)

fig.savefig(r"d:\UE5projects\Demo\claude\pcg_architecture_overview.png",
            bbox_inches="tight", facecolor="white")
print("saved")
