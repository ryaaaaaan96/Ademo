# 屏幕外窗口恢复后，原生窗口左上角仍可能超出可用桌面

Status: needs-triage

## 问题与复现

2026-10-24 在正常 Windows 用户环境、Qt 6.12.0 LLVM-MinGW、当前显示缩放下，`workspace_layout_tests::unavailableScreenGeometryReturnsToVisibleDesktop` 稳定失败。将浮动会话移至 (-20000, -20000)，保存并重新打开工作区：窗口与可用桌面相交，但 `availableGeometry().contains(frameGeometry().topLeft())` 不成立。完整 Debug、定向复验与完整 Release 都复现。

使用修改前 Release Modbus 插件（保存在 build/qt-llvm-release/live-instance-backup-config-20261024）和未修改的 Release 窗口布局测试程序独立复验，也在相同断言失败。因此此问题不依赖本次“配置…”窗口改动；当前环境结果与 2026-10-23 记录不同，不能声称已定位根因。

## 验收目标

- [ ] 查明原生边框、桌面可用区域与缩放对恢复位置的影响。
- [ ] 恢复屏幕外浮窗后，标题栏及窗口左上角在可用桌面内，正常布局恢复保持原行为。
- [ ] 通过上述公开窗口行为测试和工作区布局回归。

## 证据

[当前版本首次失败](../../multi-protocol-workbench-ui/evidence/UI13/configuration-refinement/debug-layout-first.txt)、[修改前 Release 插件对照](../../multi-protocol-workbench-ui/evidence/UI13/configuration-refinement/layout-baseline-release.txt)。

## Comments

本次仅记录独立问题，未修改宿主布局代码；参数配置收纳的界面验收单独进行。
