# Latest Error

当前无已知 Git 备份错误。

2026-07-29 已在精确核验 origin = git@git.woa.com:shiqiqiwang/Demo.git 后完成普通推送；主快照 2c97789aa6d0c1f5abf119894134e06d8995fcf1 已到达 origin/main，84 个 LFS 对象（约 40 MB）上传成功。

Level0 先前缺少完整 Recast 覆盖的问题已于 2026-07-29 解决：导航边界覆盖 V3 完整组合，静态 Recast 已重建，入口到 Deck135 五段同步路径均完整。仍存在两个已知但不阻塞静态样例的工具/运行边界：本地 MCP 的 open_asset_editor 会返回 crash_prevented，虽然地图切换实际成功；PIE 停止后出现 CrowdManager 析构期 Unable to find RecastNavMesh instance，运行阶段未出现该警告。

当前 C++ 构建、18 项 Demo.PCG、多 Seed Runtime 导航/追猎、玩家实际行走、真实 AI Actor 路线、连续命中与生命归零失败/同 Seed 重开仍未验证。
