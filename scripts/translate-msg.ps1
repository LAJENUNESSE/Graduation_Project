<#
PowerShell commit message translator for git filter-branch --msg-filter

Reads commit message from stdin, performs conservative regex-based
replacements to convert common English phrases to Simplified Chinese
while preserving structural lines like 'Co-Authored-By:' and Merge lines.

Usage:
  git filter-branch -f --msg-filter "powershell -NoProfile -ExecutionPolicy Bypass -File scripts/translate-msg.ps1" HEAD
#>

[CmdletBinding()]
param()

$orig = [Console]::In.ReadToEnd()
if ([string]::IsNullOrEmpty($orig)) {
    exit 0
}

$repls = @(
    @{p='OpenGL backend implementations for all renderer abstractions'; r='为所有渲染抽象提供 OpenGL 后端实现'},
    @{p='YAML scene serialization/deserialization via yaml-cpp'; r='通过 yaml-cpp 实现 YAML 场景序列化/反序列化'},
    @{p='Mesh primitives: Cube, Plane, Sphere with normals and UVs'; r='网格基元：立方体、平面、球体，包含法线与 UV'},
    @{p='Dockable ImGui editor with Chinese UI'; r='带停靠的 ImGui 编辑器（含中文 UI）'},
    @{p='ImGui integration with docking layout and Chinese font support'; r='集成 ImGui，支持停靠布局与中文字体'},
    @{p='Sandbox demo: rotating 3D colored cube in ImGui viewport'; r='Sandbox 示例：在 ImGui 视口中旋转的 3D 彩色立方体'},
    @{p='Entity picking via RED_INTEGER framebuffer attachment'; r='通过 RED_INTEGER 帧缓冲附件实现实体拾取'},
    @{p='Yokohama night skybox textures'; r='Yokohama 夜间天空盒纹理'},

    @{p='\bCore:\b'; r='核心：'},
    @{p='Scene system:'; r='场景系统：'},
    @{p='Texture & Material:'; r='纹理与材质：'},
    @{p='ViewManipulate:'; r='视角操作：'},
    @{p='Code review fixes:'; r='代码审查修复：'},
    @{p='Known issue:'; r='已知问题：'},

    @{p='\bCore\b'; r='核心'},
    @{p='\bApplication\b'; r='应用程序'},
    @{p='\bWindow\b'; r='窗口'},
    @{p='GLFW'; r='GLFW'},
    @{p='\bEvents\b'; r='事件'},
    @{p='\bInput\b'; r='输入'},
    @{p='\bLog\b'; r='日志'},
    @{p='Layer system'; r='图层系统'},
    @{p='Renderer abstractions'; r='渲染抽象'},
    @{p='\bBuffer\b'; r='缓冲区'},
    @{p='\bVAO\b'; r='VAO'},
    @{p='\bShader\b'; r='着色器'},
    @{p='\bTexture\b'; r='纹理'},
    @{p='\bFramebuffer\b'; r='帧缓冲'},
    @{p='\bCamera\b'; r='相机'},
    @{p='EditorCamera'; r='编辑器相机'},
    @{p='orb[iI]t'; r='环绕'},
    @{p='\bpan\b'; r='平移'},
    @{p='\bzoom\b'; r='缩放'},
    @{p='Alt\+mouse'; r='Alt+鼠标'},
    @{p='ImGui'; r='ImGui'},
    @{p='docking layout'; r='停靠布局'},
    @{p='Chinese font'; r='中文字体'},
    @{p='Sandbox'; r='Sandbox'},
    @{p='demo'; r='示例'},
    @{p='rotating'; r='旋转的'},
    @{p='3D'; r='3D'},
    @{p='colored cube'; r='彩色立方体'},
    @{p='Code review'; r='代码审查'},
    @{p='resource leaks'; r='资源泄漏'},
    @{p='null checks'; r='空检查'},
    @{p='format validation'; r='格式校验'},

    @{p='ECS with EnTT'; r='基于 EnTT 的 ECS'},
    @{p='SceneCamera with perspective/orthographic switching'; r='SceneCamera 支持透视/正交切换'},
    @{p='MeshRendererComponent'; r='MeshRenderer 组件'},

    @{p='DiffuseTexture'; r='漫反射纹理'},
    @{p='Shininess'; r='光泽'},
    @{p='u_DiffuseTexture'; r='u_DiffuseTexture'},
    @{p='u_HasTexture'; r='u_HasTexture'},
    @{p='u_Tiling'; r='u_Tiling'},
    @{p='u_Shininess'; r='u_Shininess'},

    @{p='PCF'; r='PCF'},
    @{p='DEPTH_COMPONENT'; r='DEPTH_COMPONENT'},
    @{p='Shadow pass'; r='阴影通道'},

    @{p='Editor application'; r='编辑器应用'},
    @{p='Dockable ImGui editor'; r='支持停靠的 ImGui 编辑器'},
    @{p='Properties panel'; r='属性面板'},
    @{p='ImGuizmo'; r='ImGuizmo'},
    @{p='Entity'; r='实体'},
    @{p='Camera UI'; r='相机 UI'},
    @{p='Serialization'; r='序列化'},
    @{p='backward compatibility'; r='向后兼容'},
    @{p='Code review fixes'; r='代码审查修复'},

    @{p='\bAdd(ed|ing)?\b'; r='添加'},
    @{p='\bIntegrate(d)?\b'; r='集成'},
    @{p='\bUpdate(d)?\b'; r='更新'},
    @{p='\bFix(es|ed)?\b'; r='修复'},
    @{p='\bRefactor(ed)?\b'; r='重构'},
    @{p='\bChore\b'; r='杂项'},
    @{p='\btest\b'; r='测试'}
)

function Replace-All([string]$text) {
    $out = $text
    foreach ($entry in $repls) {
        try {
            $out = [regex]::Replace($out, $entry.p, $entry.r, [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
        } catch {
            # If a particular regex is invalid for some line, skip it
        }
    }
    return $out
}

# Split into lines preserving empty lines
$lines = $orig -split "\r?\n"
if ($lines.Length -eq 0) {
    exit 0
}

# Handle subject line keeping conventional prefix like feat(scope): intact
$subject = $lines[0]
$subRegex = '^(?<prefix>[a-zA-Z0-9\-]+(\([^)]*\))?:)\s*(?<rest>.*)$'
$m = [regex]::Match($subject, $subRegex)
if ($m.Success) {
    $prefix = $m.Groups['prefix'].Value
    $rest = $m.Groups['rest'].Value
    $newRest = Replace-All $rest
    $newSubject = "$prefix $newRest"
} else {
    $newSubject = Replace-All $subject
}

$outLines = New-Object System.Collections.Generic.List[string]
$outLines.Add($newSubject)

for ($i = 1; $i -lt $lines.Length; $i++) {
    $line = $lines[$i]
    if ([string]::IsNullOrWhiteSpace($line)) {
        $outLines.Add($line)
        continue
    }
    if ($line.TrimStart().StartsWith('Co-Authored-By:') -or $line.StartsWith('Merge branch') -or $line.StartsWith('Merge pull request') -or $line.StartsWith('Merge ')) {
        $outLines.Add($line)
        continue
    }
    $mList = [regex]::Match($line, '^(\s*[-*]\s*)(.*)$')
    if ($mList.Success) {
        $lead = $mList.Groups[1].Value
        $tail = $mList.Groups[2].Value
        $outLines.Add($lead + (Replace-All $tail))
    } else {
        $outLines.Add((Replace-All $line))
    }
}

[Console]::Out.WriteLine(($outLines -join "`n"))
