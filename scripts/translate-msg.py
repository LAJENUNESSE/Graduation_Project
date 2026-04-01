#!/usr/bin/env python3
"""
Lightweight commit message translator (English -> 简体中文)

This script is intended to be used as a git --msg-filter helper.
It reads the current commit message from stdin and uses heuristic
regex replacements to translate common English phrases/terms to
Simplified Chinese while preserving structural lines such as
"Co-Authored-By:" and merge lines. It deliberately avoids
changing code-like identifiers (camelCase/underscores) by matching
whole words and longer phrases first.

Usage (example):
  git filter-branch -f --msg-filter "python scripts/translate-msg.py" -- <ref>

Notes:
- This is a conservative, heuristic translator (not a machine
  translation API). It focuses on keeping format and common
  technical phrases translated while leaving variable names,
  flags and commit-type prefixes intact.
"""
from __future__ import annotations
import os
import re
import sys


def load_replacements():
    # Ordered list: longer phrases first to avoid partial matches
    pats = [
        # Big phrases / headers
        (r'OpenGL backend implementations for all renderer abstractions', '为所有渲染抽象提供 OpenGL 后端实现'),
        (r'YAML scene serialization/deserialization via yaml-cpp', '通过 yaml-cpp 实现 YAML 场景序列化/反序列化'),
        (r'Mesh primitives: Cube, Plane, Sphere with normals and UVs', '网格基元：立方体、平面、球体，包含法线与 UV'),
        (r'Dockable ImGui editor with Chinese UI', '带停靠的 ImGui 编辑器（含中文 UI）'),
        (r'ImGui integration with docking layout and Chinese font support', '集成 ImGui，支持停靠布局与中文字体'),
        (r'Sandbox demo: rotating 3D colored cube in ImGui viewport', 'Sandbox 示例：在 ImGui 视口中旋转的 3D 彩色立方体'),
        (r'Entity picking via RED_INTEGER framebuffer attachment', '通过 RED_INTEGER 帧缓冲附件实现实体拾取'),
        (r'Yokohama night skybox textures', 'Yokohama 夜间天空盒纹理'),

        # Common headers and labels
        (r'Core:', '核心：'),
        (r'Scene system:', '场景系统：'),
        (r'Texture & Material:', '纹理与材质：'),
        (r'ViewManipulate:', '视角操作：'),
        (r'Code review fixes:', '代码审查修复：'),
        (r'Known issue:', '已知问题：'),

        # Short words / technical terms (word boundaries)
        (r'\bCore\b', '核心'),
        (r'\bApplication\b', '应用程序'),
        (r'\bWindow\b', '窗口'),
        (r'GLFW', 'GLFW'),
        (r'\bEvents\b', '事件'),
        (r'\bInput\b', '输入'),
        (r'\bLog\b', '日志'),
        (r'Layer system', '图层系统'),
        (r'Renderer abstractions', '渲染抽象'),
        (r'\bBuffer\b', '缓冲区'),
        (r'\bVAO\b', 'VAO'),
        (r'\bShader\b', '着色器'),
        (r'\bTexture\b', '纹理'),
        (r'\bFramebuffer\b', '帧缓冲'),
        (r'\bCamera\b', '相机'),
        (r'EditorCamera', '编辑器相机'),
        (r'orb[iI]t', '环绕'),
        (r'pan', '平移'),
        (r'zoom', '缩放'),
        (r'Alt\+mouse', 'Alt+鼠标'),
        (r'ImGui', 'ImGui'),
        (r'docking layout', '停靠布局'),
        (r'Chinese font', '中文字体'),
        (r'Sandbox', 'Sandbox'),
        (r'demo', '示例'),
        (r'rotating', '旋转的'),
        (r'3D', '3D'),
        (r'colored cube', '彩色立方体'),
        (r'Code review', '代码审查'),
        (r'resource leaks', '资源泄漏'),
        (r'null checks', '空检查'),
        (r'format validation', '格式校验'),

        # Scene / ECS
        (r'ECS with EnTT', '基于 EnTT 的 ECS'),
        (r'SceneCamera with perspective/orthographic switching', 'SceneCamera 支持透视/正交切换'),
        (r'MeshRendererComponent', 'MeshRenderer 组件'),

        # Texture & material
        (r'DiffuseTexture', '漫反射纹理'),
        (r'Shininess', '光泽'),
        (r'u_DiffuseTexture', 'u_DiffuseTexture'),
        (r'u_HasTexture', 'u_HasTexture'),
        (r'u_Tiling', 'u_Tiling'),
        (r'u_Shininess', 'u_Shininess'),

        # Shadow / PCF
        (r'PCF', 'PCF'),
        (r'DEPTH_COMPONENT', 'DEPTH_COMPONENT'),
        (r'Shadow pass', '阴影通道'),

        # Misc
        (r'Editor application', '编辑器应用'),
        (r'Dockable ImGui editor', '支持停靠的 ImGui 编辑器'),
        (r'Properties panel', '属性面板'),
        (r'ImGuizmo', 'ImGuizmo'),
        (r'Entity', '实体'),
        (r'Camera UI', '相机 UI'),
        (r'Serialization', '序列化'),
        (r'backward compatibility', '向后兼容'),
        (r'Code review fixes', '代码审查修复'),

        # Common verbs
        (r'Add(ed|ing)?', '添加'),
        (r'Integrate', '集成'),
        (r'Integrate[d]?', '集成'),
        (r'Update', '更新'),
        (r'Updated', '更新'),
        (r'Fix(es|ed)?', '修复'),
        (r'Refacto?r', '重构'),
        (r'Refactored', '重构'),
        (r'Chore', '杂项'),
        (r'Add README', '添加 README'),
        (r'Co-Authored-By:', 'Co-Authored-By:'),
    ]
    # Compile into list of (compiled_regex, replacement)
    compiled = []
    for p, r in pats:
        compiled.append((re.compile(p, flags=re.IGNORECASE), r))
    return compiled


REPLS = load_replacements()


def translate_line(line: str) -> str:
    # Preserve structural lines unchanged
    if line.strip().startswith('Co-Authored-By:'):
        return line
    if line.startswith('Merge branch') or line.startswith('Merge pull request') or line.startswith('Merge '):
        # Translate only the descriptive part after the verb if needed
        return line

    out = line
    # Apply replacements
    for cre, rep in REPLS:
        out = cre.sub(rep, out)
    return out


def translate_message(msg: str) -> str:
    # Split lines and translate selectively
    lines = msg.splitlines()
    if not lines:
        return msg

    new_lines = []
    # Subject line handling: keep the commit type prefix (e.g., feat:, fix:) intact
    subj = lines[0]
    m = re.match(r'^(?P<prefix>[a-zA-Z\-]+(:|\([^)]+\):))\s*(?P<rest>.*)$', subj)
    if m:
        prefix = m.group('prefix')
        rest = m.group('rest')
        rest_t = rest
        # translate the rest of subject
        for cre, rep in REPLS:
            rest_t = cre.sub(rep, rest_t)
        new_lines.append(prefix + ' ' + rest_t)
    else:
        # No conventional prefix, translate whole subject conservatively
        new_lines.append(translate_line(subj))

    # Translate remaining lines but preserve blank lines and Co-Authored-By
    for line in lines[1:]:
        if line.strip() == '':
            new_lines.append(line)
            continue
        # Preserve 'Co-Authored-By' and merge lines
        if line.strip().startswith('Co-Authored-By:') or line.startswith('Merge branch') or line.startswith('Merge pull request'):
            new_lines.append(line)
            continue
        # For list items keep leading whitespace+dash and translate tail
        mlist = re.match(r'^(\s*[-*]\s*)(.*)$', line)
        if mlist:
            lead, tail = mlist.groups()
            new_lines.append(lead + translate_line(tail))
        else:
            new_lines.append(translate_line(line))

    return '\n'.join(new_lines)


def main() -> None:
    orig = sys.stdin.read()
    translated = translate_message(orig)
    # Print translated message to stdout (git expects the new message on stdout)
    sys.stdout.write(translated)


if __name__ == '__main__':
    main()
