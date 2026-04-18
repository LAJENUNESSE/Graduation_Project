; =========================================================================
; GameEngine Editor — Windows 安装器 (Inno Setup 6+)
;
; 前置步骤:
;   1. 构建 Editor (RelWithDebInfo):
;        cmake --preset default
;        cmake --build build --config RelWithDebInfo --target Editor
;   2. 生成 CPack staging 目录 (供此脚本取用):
;        cpack --config build/CPackConfig.cmake -G ZIP -C RelWithDebInfo -B build
;   3. 下载 VC++ 2022 运行库到 installer/redist/:
;        https://aka.ms/vs/17/release/vc_redist.x64.exe
;   4. 编译安装器 (在项目根目录执行):
;        "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\Editor.iss
;   5. 产物: dist/GameEngineEditor-Setup-0.1.0.exe
; =========================================================================

#define MyAppName "GameEngine Editor"
#define MyAppNameInternal "GameEngineEditor"
#define MyAppVersion "0.1.0"
#define MyAppPublisher "LAJENUNESSE"
#define MyAppURL "https://github.com/LAJENUNESSE/Graduation_Project"
#define MyAppExeName "Editor.exe"
; CPack ZIP generator 的 staging 目录 (相对于此 .iss 文件)
#define MyAppStagingDir "..\build\_CPack_Packages\win64\ZIP\" + MyAppNameInternal + "-" + MyAppVersion + "-win64"

[Setup]
; AppId 在首次安装后写入注册表，升级时根据它定位旧版本，请勿修改
AppId={{7B8E4E3D-4F8B-4E7C-9A0B-1E2F3A4B5C6D}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
AppUpdatesURL={#MyAppURL}/releases
DefaultDirName={autopf}\{#MyAppNameInternal}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=auto
OutputDir=..\dist
OutputBaseFilename={#MyAppNameInternal}-Setup-{#MyAppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
WizardStyle=modern
LicenseFile=..\LICENSE
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
UninstallDisplayIcon={app}\Editor\{#MyAppExeName}
UninstallDisplayName={#MyAppName} {#MyAppVersion}
; Windows 10 1809 及以上 (PrivilegesRequiredOverridesAllowed 需要 Inno 6+)
MinVersion=10.0.17763

[Languages]
; Inno Setup 6.3+ 将简体中文文件更名为 Chinese.isl（之前是 ChineseSimplified.isl）
Name: "chinese"; MessagesFile: "compiler:Languages\Chinese.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; \
    GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; CPack staging 目录整包复制到 {app}
; 注意: 必须先跑 cpack 生成这个目录，否则 ISCC 会找不到源文件
Source: "{#MyAppStagingDir}\*"; DestDir: "{app}"; \
    Flags: recursesubdirs createallsubdirs ignoreversion

; Visual C++ 2022 运行库 (deleteafterinstall: 安装后从 {tmp} 删掉)
Source: "redist\VC_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\Editor\{#MyAppExeName}"; \
    WorkingDir: "{app}\Editor"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\Editor\{#MyAppExeName}"; \
    WorkingDir: "{app}\Editor"; Tasks: desktopicon

[Run]
; 安装 VC++ 运行库 (静默 / 不重启 / 等待完成)
; 退出码: 0=成功, 3010/1641=需重启, 1638=已装新版本
Filename: "{tmp}\VC_redist.x64.exe"; Parameters: "/install /quiet /norestart"; \
    StatusMsg: "正在安装 Microsoft Visual C++ 2022 运行库..."; \
    Flags: waituntilterminated

; 安装完成后可选启动 Editor
Filename: "{app}\Editor\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; \
    WorkingDir: "{app}\Editor"; Flags: postinstall nowait skipifsilent
