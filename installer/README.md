# Installer — Windows 安装器构建

本目录用于生成 `GameEngine Editor` 的 Windows 安装程序 (`.exe`)。

## 前置依赖

1. **Inno Setup 6.3+**（6.3 起简体中文文件名从 `ChineseSimplified.isl` 改为 `Chinese.isl`）
   - 下载: https://jrsoftware.org/isdl.php
   - 官方默认路径: `C:\Program Files (x86)\Inno Setup 6\ISCC.exe`
   - 本项目实际使用: `C:\Dev\Tools\Inno Setup 6\ISCC.exe`（按本机安装位置调整）

2. **Visual C++ 2022 x64 运行库**
   - 下载: https://aka.ms/vs/17/release/vc_redist.x64.exe
   - 放到 `installer/redist/VC_redist.x64.exe`（此文件不入库，需手动下载）

## 构建步骤

在**项目根目录**执行：

```bash
# 1. Configure (首次或改了 CMakeLists.txt 后)
"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --preset default

# 2. 构建 Editor (RelWithDebInfo)
"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config RelWithDebInfo --target Editor

# 3. 生成 CPack staging 目录（Inno 会从这里取文件）
"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cpack.exe" --config build/CPackConfig.cmake -G ZIP -C RelWithDebInfo -B build

# 4. 编译安装器（按本机 Inno Setup 安装路径调整）
"C:/Dev/Tools/Inno Setup 6/ISCC.exe" installer/Editor.iss
```

产物：`dist/GameEngineEditor-Setup-0.1.0.exe`

## 安装器功能

- 安装到 `Program Files\GameEngineEditor\`（可选其它目录）
- 自动静默安装 VC++ 2022 运行库
- 创建开始菜单快捷方式，可选桌面快捷方式
- 简体中文 + 英文双语界面
- 标准卸载器 (控制面板 / 卸载程序)

## 目录布局

```
installer/
├── Editor.iss           # Inno Setup 主脚本
├── README.md            # 本文件
└── redist/
    └── VC_redist.x64.exe  # 手动下载，不入库
```
