#!/usr/bin/env bash
# 编译教材 PDF。依赖：MiKTeX / TeX Live 提供 latexmk + xelatex。
set -e
cd "$(dirname "$0")"
latexmk main.tex
echo "输出: $(pwd)/main.pdf"
