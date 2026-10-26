# RTC Talk Qt Widgets UI Prototype

这是独立于 `client-win` 主工程的 Qt Widgets GUI 原型，用于先搭建和调试桌面界面。
GUI mock 第一版 M1–M6 已完成并确认，不接入 WebRTC、CoreClient、信令或真实设备。

## 当前状态

已完成：

- M1：应用壳、页面切换、无边框窗口与房间页骨架
- M2：多人宫格、聚焦、1v1、屏幕共享和视频过滤
- M3：房间控制与状态同步
- M4：成员、设备、状态面板业务交互
- M5：弹窗、诊断和视觉收口
- M6：六场景回归、最低尺寸、125%/150% 缩放、冒烟测试与交接文档
  - 无边框设置弹窗与离开确认弹窗
  - 设置弹窗与当前音源、设备、音量、预览状态同步
  - 右侧诊断抽屉、点击遮罩关闭
  - 诊断打开时隐藏主窗口控制按钮，避免标题区重叠
  - 下拉框 chevron 箭头
  - Pill、SegmentedControl 完整状态样式
  - 1100×720 响应式适配

后续模块：

- M6：完整场景截图、DPI、冒烟测试与交接文档

详细计划见 `docs/DELIVERY_PLAN.md`。

## 构建

```powershell
cmake --build .\build --config RelWithDebInfo --parallel
```

生成文件：

```text
build/RelWithDebInfo/rtc_talk_qt_ui.exe
```

## 启动与场景

```powershell
# 默认入房页
.\build\RelWithDebInfo\rtc_talk_qt_ui.exe

# 指定场景、面板和尺寸
.\build\RelWithDebInfo\rtc_talk_qt_ui.exe --scenario multi --panel members
.\build\RelWithDebInfo\rtc_talk_qt_ui.exe --scenario multi --panel devices
.\build\RelWithDebInfo\rtc_talk_qt_ui.exe --scenario multi --panel status
.\build\RelWithDebInfo\rtc_talk_qt_ui.exe --scenario multi --size 1100x720

# 打开诊断
.\build\RelWithDebInfo\rtc_talk_qt_ui.exe --scenario diagnostics

# 弹窗视觉验证
.\build\RelWithDebInfo\rtc_talk_qt_ui.exe --scenario multi --show-settings
.\build\RelWithDebInfo\rtc_talk_qt_ui.exe --scenario multi --show-leave

# 模拟操作
.\build\RelWithDebInfo\rtc_talk_qt_ui.exe --scenario multi --action mic --action camera
.\build\RelWithDebInfo\rtc_talk_qt_ui.exe --scenario multi --action muteall
.\build\RelWithDebInfo\rtc_talk_qt_ui.exe --scenario multi --panel devices --action source-system
```

## M5 验收截图

```text
build/shots/m5-settings.png
build/shots/m5-leave.png
build/shots/m5-diagnostics.png
build/shots/m5-devices-arrows.png
build/shots/m5-responsive-1100.png
```

## M5 手工验收

1. 点击设置，检查弹窗内容、无边框样式和控件状态
2. 切换系统音频，确认麦克风设备控件禁用
3. 修改音量和预览，确认房间页状态同步
4. 点击离开，验证取消和确认两个流程
5. 打开诊断，检查日志、统计和抽屉布局
6. 点击遮罩关闭诊断，确认窗口控制按钮恢复
7. 点击关闭按钮关闭诊断，确认不会误触发遮罩
8. 在 1100×720 下检查头部、侧栏和底部控制栏

## 当前限制

- 设备列表和统计数据仍为 mock
- 设置弹窗和诊断抽屉暂未接入真实持久化与诊断数据
- 真实 CoreClient/WebRTC 接入属于 GUI 第一版之后的独立阶段
