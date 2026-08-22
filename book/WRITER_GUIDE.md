# 章节扩写代理指南（内部工作文件）

## 任务
把 book/chapters/ 下指定的占位卡文件扩写为完整章节（覆盖原文件）。每章目标
180–280 行 TeX，详略基准 = ch02-core-loop.tex。

## 必读（动笔前按序读完）
1. book/OUTLINE.md — 你负责章节的占位卡（时间线/锚点/素材线索全在里面）
2. book/chapters/ch02-core-loop.tex — 风格与结构基准，必须对齐
3. 占位卡列出的源码锚点文件 — 亲自 Read，代码摘录必须来自真实文件
4. 占位卡列出的 docs/ 文档（如有）— 提取决策与踩坑素材

## 五段式结构（与样章一致）
\section{问题引入} → \section{通用原理：…} → \section{本项目的设计与决策}
（可拆 subsections）→ \section{实现走读：…} → \section{踩坑记录}
（description 列表 3-5 条）→ \section{小结与延伸思考}

## 写作纪律（硬约束）
1. **每个论断给证据**：代码锚点写 `Engine/src/...` 路径；历史事实写 git 提交短哈希；
   引用文档写 docs/ 路径。写前必须验证哈希存在且语义相符：
   `git -C "C:/Dev/Workspace/C++/Graduation_Project" log --format="%h %s" | grep <hash>`
2. **代码摘录用 lstlisting**，语言=C++ 或 GLSL（language=[GLSL]C++），每段 ≤35 行，
   摘录后必须有逐段中文讲解。注释保持英文（listings 中文易碎）。
3. **LaTeX 安全**：下划线一律 `\_`；`#` `$` `%` `&` 转义；不用 minted、不用
   未定义环境（可用：itemize/enumerate/description/lstlisting/tabular/tcolorbox/
   definition/theorem/example/notice/exercise/center）；数学用 $...$。
   不要 \label/\ref 跨章引用。不要自造 \newcommand。
4. **中文行文**，技术名词保留英文。语气：工程师复盘，不是论文腔。
5. 禁止虚构：不确定的细节宁可不写，不要编造提交号/类名/行为。
6. 完成后自查：grep 文件中的每个提交哈希确认真实存在。

## 输出
直接覆盖写入你负责的 .tex 文件，不建新文件，不动其他章。
