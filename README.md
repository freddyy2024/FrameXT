# FrameXT · 照片边框工具（Qt C++ 重构版）

给照片加相机品牌边框与拍摄信息水印的桌面工具。原版为 Flutter + Go DLL（libvips）架构，
本仓库为 Qt C++ 重构版：**纯 Qt 自绘渲染管线 + exiv2 元数据保真写回**，规划开源（GPLv3+）。

## 设计文档

上游产品/技术/UI 文档位于仓库外层 `产品/202610-FRAMEXT照片边框工具/`：

| 文档 | 内容 |
|---|---|
| 01-产品重构方案.md | 原产品功能全景、GitHub issues 与 B 站反馈分析、需求规划 |
| 02-技术设计.md | 四层架构、渲染管线、元数据引擎、决策记录（v1.1） |
| 03-开发计划.md | M0–M6 里程碑、质量门、风险预案 |
| 04-UI设计.md（+HTML 稿） | 浅色主题 Token、界面规格（全局圆角 5px） |

## 构建（Windows）

依赖：Qt 6.11（msvc2022_64）、VS2022（x64 工具链）、CMake ≥3.24 + Ninja、vcpkg（exiv2 自 M2 启用）。

Qt / Ninja / vcpkg 路径目前写在 `CMakePresets.json` 的 `win-base` 预设中（本机配置，
后续迁移到 `CMakeUserPresets.json` / 环境变量）。在 VS2022 开发者命令行（x64）中执行：

```powershell
cmake --preset win-debug
cmake --build --preset win-debug
ctest --preset win-debug
```

可执行文件：`build/win-debug/framext.exe`

> M0 骨架阶段（无 vcpkg 依赖）可加 `-DVCPKG_MANIFEST_INSTALL=OFF` 跳过 manifest 安装。

## 打包（Windows）

在 VS2022 开发者命令行（x64）中执行 `scripts\package-win.bat`，一次产出两种发行物（`dist/`）：

| 产物 | 说明 |
|---|---|
| `FrameXT-<版本>-setup.exe` | Inno Setup 安装包：简体中文向导、带应用图标、可选桌面快捷方式；用户数据在 `%APPDATA%\FrameXT` |
| `FrameXT-<版本>-portable.zip` | 免安装便携版：解压即用、无需管理员权限；包根的 `FrameXT.portable` 空标记文件使数据（设置 / 品牌别名库 / 自定义模板）落在程序目录 `Data/` 下——整个文件夹可 U 盘携带，删除即卸载 |

便携检测逻辑见 `src/core/AppPaths.cpp`；安装包不含标记文件，行为不受影响。

## 目录结构

```
framext-qt/
├── src/
│   ├── app/          # 应用壳：主窗口、自绘标题栏、装配（Qt6::Widgets）
│   ├── controller/   # 项目/导出/导入控制器（M3）
│   ├── core/         # 领域模型与纯计算（仅 Qt6::Core，分层受 CMake 约束）
│   ├── render/       # 渲染管线 S1–S8（M1）
│   └── platform/     # JPEG/PNG 段级 IO、元数据写回（M2）
├── resources/
│   ├── theme/        # 浅色主题 QSS（Token 对齐 04-UI设计.md）
│   ├── icons/        # 应用图标（logo.ico / logo.png / 1024px）
│   ├── logos/camera/ # 16 相机品牌 Logo（版权归各厂商，仅用于品牌识别）
│   ├── fonts/        # 字体资产（许可核查后入库）
│   ├── templates/    # 内置/自定义模板 JSON（M5）
│   └── i18n/         # zh-CN / zh-TW / en 翻译（M3）
├── tests/            # 单元测试（仅依赖 core）
└── scripts/          # 工具脚本（基准测试、打包）
```

## 许可

GPLv3 或更高版本（见 LICENSE）。`resources/logos/camera/` 下的品牌 Logo
版权归各相机厂商所有，本项目仅在品牌识别场景下引用。
