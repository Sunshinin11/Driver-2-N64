#!/usr/bin/env python3
"""Desfaz no fonte do motor um P(...) de tools/patch_engine.py identificado por um comentario-marcador.
Uso: python3 tools/reverter_patch.py <patch_engine.py> <marcador> [--remover]
Procura o primeiro P(...) depois da linha que contem <marcador>, troca o texto 'novo' pelo 'antigo' no arquivo do motor
(port/redriver2/src_rebuild/<arquivo>) e, com --remover, tira o bloco (marcador ate o fim do P) do patch_engine.py.
Serve para montar a arvore de comparacao (A/B) de um lote sem perder o original."""
import sys, ast, os
pe, marker = sys.argv[1], sys.argv[2]
remove = '--remover' in sys.argv
src = open(pe, encoding='utf-8').read()
i = src.index(marker)
j = src.index("P(", i)
tree = ast.parse(src[j:], mode='exec')          # o primeiro statement e o P(...)
call = tree.body[0].value
path, old, new = (ast.literal_eval(a) for a in call.args[:3])
end = j + len(ast.get_source_segment(src[j:], tree.body[0]))
root = os.path.join(os.path.dirname(os.path.abspath(pe)), '..', 'port', 'redriver2', 'src_rebuild')
fp = os.path.join(root, path)
s = open(fp, encoding='latin-1', newline='').read()
crlf = '\r\n' in s
o, n = (old.replace('\n', '\r\n'), new.replace('\n', '\r\n')) if crlf else (old, new)
if n in s:
    s = s.replace(n, o, 1); open(fp, 'w', encoding='latin-1', newline='').write(s); print('revertido em', path)
elif o in s:
    print('ja estava no original:', path)
else:
    sys.exit('texto do patch nao encontrado em ' + path)
if remove:
    line0 = src.rfind('\n', 0, i) + 1
    open(pe, 'w', encoding='utf-8').write(src[:line0] + src[end:].lstrip('\n'))
    print('bloco removido de', pe)
