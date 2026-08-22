# 教材编译配置（latexmk）
# 用法：在 book/ 目录下执行 latexmk main.tex
$pdf_mode = 5;              # xelatex 经 xdvipdfmx 出 PDF
$xelatex  = 'xelatex -halt-on-error -interaction=nonstopmode -synctex=1 %O %S';
$bibtex_use = 2;
$clean_ext = 'synctex.gz run.xml bbl bcf lof lot out toc xdv';
