import glob
from markitdown import MarkItDown

md = MarkItDown()
# 只读末期答辩未完成这个文件
targets = glob.glob(r'd:/UE5projects/Demo/DOC/PPT/*.pptx')
targets = [t for t in targets if '末期' in t and not t.split('\\')[-1].startswith('~$')]

for t in targets:
    print('FILE:', t)
    r = md.convert(t)
    print(r.text_content)
