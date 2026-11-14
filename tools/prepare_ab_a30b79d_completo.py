"""Prepara a30b79d + somente integracao de conteudo completo, em pasta vazia.
Nao copia fontes da arvore de trabalho. Usa apenas dois commits fixados do Git.
"""
import argparse, ast, hashlib, io, json, subprocess, tarfile
from pathlib import Path

BASE = 'a30b79da425814c27c1a5405fdb670ac32d69d6c'
CONTENT = '49edd99'
ROOT = Path(__file__).resolve().parents[1]

def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args])

def blob(ref, name):
    return git('show', f'{ref}:{name}')

def system_patches(ref):
    source = blob(ref, 'tools/patch_engine.py').decode('utf-8')
    result = []
    for node in ast.parse(source).body:
        if not (isinstance(node, ast.Expr) and isinstance(node.value, ast.Call)
                and isinstance(node.value.func, ast.Name) and node.value.func.id == 'P'):
            continue
        try:
            values = tuple(ast.literal_eval(a) for a in node.value.args)
        except (ValueError, TypeError):
            continue
        if values[0] == 'Game/C/system.c':
            result.append((values, ast.get_source_segment(source, node)))
    return result

def prepare(destination):
    destination = destination.resolve()
    destination.mkdir(parents=True, exist_ok=True)
    if any(destination.iterdir()):
        raise RuntimeError('Destino deve estar vazio; nenhum arquivo existente sera substituido.')
    content_ref = git('rev-parse', CONTENT+'^{commit}').decode().strip()
    original = {}
    with tarfile.open(fileobj=io.BytesIO(git('archive', BASE))) as archive:
        for member in archive:
            if member.isdir():
                continue
            if not member.isfile() or Path(member.name).is_absolute() or '..' in Path(member.name).parts:
                raise RuntimeError('Entrada inesperada no git archive: '+member.name)
            data = archive.extractfile(member).read()
            original[member.name] = data
            path = destination/member.name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
    def write(name, text):
        (destination/name).write_text(text, encoding='utf-8', newline='\n')
    def text(name):
        return (destination/name).read_text(encoding='utf-8')
    # Somente ferramentas de conversao e carregador do pacote original.
    for name in ('tools/pack_levels.py', 'tools/convert_xa_wav64.py',
                 'tools/convert_fmv_n64.py', 'port/n64/fs_n64.cpp'):
        (destination/name).write_bytes(blob(content_ref, name))
    # Acrescenta apenas os sete patches de acesso a dados do LEVPACK.
    existing = {values for values, _ in system_patches(BASE)}
    additions = [code for values, code in system_patches(content_ref) if values not in existing]
    if len(additions) != 7:
        raise RuntimeError('Quantidade inesperada de patches de conteudo.')
    source = text('tools/patch_engine.py')
    anchor = 'def main():'
    if source.count(anchor) != 1:
        raise RuntimeError('Ponto de insercao do patch nao encontrado.')
    source = source.replace(anchor, '# Somente acesso ao pacote de conteudo completo.\n'
                            +'\n'.join(additions)+'\n\n'+anchor, 1)
    ast.parse(source)
    write('tools/patch_engine.py', source)
    # Habilita o codec dos arquivos convertidos; nao altera volumes ou mixer.
    name = 'port/n64/psyx_backend.cpp'
    source = text(name)
    anchor = 'audio_init(32000, 4); mixer_init(D2_SPU_VOICES + 1);'
    if source.count(anchor) != 1:
        raise RuntimeError('Inicializacao de audio inesperada.')
    write(name, source.replace(anchor, anchor+' wav64_init_compression(2);'))
    name = 'port/n64/fmv_n64.cpp'
    source = text(name)
    anchor = 'mixer_init(1);'
    if source.count(anchor) != 1:
        raise RuntimeError('Inicializacao de FMV inesperada.')
    write(name, source.replace(anchor, anchor+'\n            wav64_init_compression(2);'))
    # Preserva -Os/selecao -O2, tipo de save e flags de a30b79d.
    name = 'Makefile.d2'; source = text(name)
    source = source.replace('CITIES  ?= CHICAGO\n', 'CITIES  ?= CHICAGO,HAVANA,RIO,VEGAS\n')
    source = source.replace('FMV_IDS ?= 0\n', 'FMV_IDS ?= all\n')
    anchor = '\t@echo "    [DFS] $@"'
    if source.count(anchor) != 1:
        raise RuntimeError('Regra DFS inesperada.')
    source = source.replace(anchor, '\t@python3 tools/pack_levels.py $(FS_STAGE)/DRIVER2 $(FS_STAGE)/DRIVER2/LEVPACK.BIN --remove\n'+anchor)
    write(name, source)
    allowed = {'Makefile.d2', 'tools/patch_engine.py', 'port/n64/psyx_backend.cpp',
               'port/n64/fmv_n64.cpp', 'port/n64/fs_n64.cpp', 'tools/pack_levels.py',
               'tools/convert_xa_wav64.py', 'tools/convert_fmv_n64.py'}
    changed = {name for name, data in original.items() if (destination/name).read_bytes() != data}
    added = {str(p.relative_to(destination)).replace('\\','/') for p in destination.rglob('*') if p.is_file()} - original.keys()
    if changed | added != allowed:
        raise RuntimeError('Alteracoes fora do escopo: '+repr(changed | added))
    manifest = {'base': BASE, 'content_source': content_ref,
                'changed_files': sorted(changed | added),
                'system_patches_added': len(additions),
                'unchanged_base_files_verified': len(original)-len(changed),
                'sha256': {name: hashlib.sha256((destination/name).read_bytes()).hexdigest() for name in sorted(allowed)},
                'notes': ['Renderer, carros, chao, paletas, logica, flags e hot.ld preservados da base.',
                          'LEVPACK original de um bloco; sem cache ou perfil recentes.',
                          'Limitacoes originais da base, incluindo carro especial desabilitado, preservadas.',
                          'Full content nao equivale a validar todas as missoes e cutscenes.']}
    write('AB_CONTENT_MANIFEST.json', json.dumps(manifest, ensure_ascii=False, indent=2)+'\n')
    return manifest

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(prepare(args.out), ensure_ascii=False, indent=2))
