#!/usr/bin/env python3
"""Converte os efeitos sonoros do PS1 (SOUND/VOICES2.BLK) para WAV64/VADPCM mono (libdragon).

Cada lump de VOICES2.BLK = uint32 num_samples + SAMPLE_DATA[num] (address, length, loop, samplerate; LE)
+ ADPCM do SPU (16 bytes = 28 amostras; byte 1: bit0 fim, bit1 repete, bit2 inicio do loop).

Saida: <out>/Lxx_Syy.wav64, uma por amostra (lump xx, indice yy dentro do lump), mantendo os
identificadores do jogo. O jogo continua usando SAMPLE_DATA (taxa original, tamanho, loop) para
pitch, duracao e eventos; o backend so troca a fonte do audio (ver psyx_backend.cpp, D2_VADPCM).

Taxa de saida: min(taxa original, --rate) (padrao 12000 Hz). Amostras com taxa original menor que
12 kHz (vozes a 5-11 kHz) NAO sao reamostradas para cima: isso so dobraria o tamanho sem ganho.
Loop: igual ao backend atual (d2_spu_shape): repete a amostra inteira quando o ultimo bloco tem bit1;
todos os loops de VOICES2.BLK comecam no bloco 0 (verificado na conversao, aborta se nao).

Uso: python3 tools/convert_sfx_wav64.py --blk filesystem_d2/DRIVER2/SOUND/VOICES2.BLK --out filesystem_sfx64/SFX64
     [--lumps 1,2-18] [--rate 12000]
"""
import argparse, os, struct, subprocess, sys, tempfile, wave

AUDIOCONV = os.environ.get('AUDIOCONV64', '/opt/libdragon/bin/audioconv64')
F0 = (0, 60, 115, 98, 122)
F1 = (0, 0, -52, -55, -60)


def decode_sample(data, off, end):
    """Decodifica como d2_spu_decode_28/d2_spu_shape. Retorna (pcm, repeat, loop_block)."""
    pcm = []
    s1 = s2 = 0
    blocks = 0
    loop_blk = -1
    repeat = False
    while off + blocks * 16 + 16 <= end and blocks < 8192:
        p = data[off + blocks * 16: off + blocks * 16 + 16]
        flags = p[1]
        if flags & 4:
            loop_blk = blocks
        shift = p[0] & 15
        filt = p[0] >> 4
        if shift > 12: shift = 12
        if filt > 4: filt = 0
        k0, k1, sh = F0[filt], F1[filt], 12 - shift
        for b in range(2, 16):
            byte = p[b]
            for n in (byte & 15, byte >> 4):
                if n & 8: n -= 16
                v = (n << sh) + ((s1 * k0 + s2 * k1 + 32) >> 6)
                v = max(-32768, min(32767, v))
                pcm.append(v)
                s2, s1 = s1, v
        blocks += 1
        if flags & 1:
            repeat = bool(flags & 2)
            break
    return pcm, repeat, loop_blk, blocks


def parse_lumps(spec):
    out = set()
    for part in spec.split(','):
        part = part.strip()
        if not part: continue
        if '-' in part:
            a, b = part.split('-'); out.update(range(int(a), int(b) + 1))
        else:
            out.add(int(part))
    return sorted(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--blk', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--lumps', default='0-71')
    ap.add_argument('--rate', type=int, default=12000)
    a = ap.parse_args()

    data = open(a.blk, 'rb').read()
    limits = struct.unpack('<73I', data[:73 * 4])
    os.makedirs(a.out, exist_ok=True)
    lumps = parse_lumps(a.lumps)
    tot_in = tot_out = nconv = 0
    with tempfile.TemporaryDirectory() as tmp:
        for lump in lumps:
            s, e = limits[lump], limits[lump + 1]
            if e <= s or s == 0:
                print(f'lump {lump}: vazio, ignorado'); continue
            n = struct.unpack('<I', data[s:s + 4])[0]
            hdr_end = s + 4 + n * 16
            for i in range(n):
                addr, length, loop, sr = struct.unpack('<4I', data[s + 4 + i * 16: s + 20 + i * 16])
                pcm, repeat, loop_blk, blocks = decode_sample(data, hdr_end + addr, e)
                if repeat and loop_blk > 0:
                    sys.exit(f'L{lump:02d}_S{i:02d}: loop a partir do bloco {loop_blk} nao suportado')
                rate = min(sr, a.rate) if sr > 0 else a.rate
                name = f'L{lump:02d}_S{i:02d}'
                wav = os.path.join(tmp, name + '.wav')
                with wave.open(wav, 'wb') as w:
                    w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
                    w.writeframes(struct.pack('<%dh' % len(pcm), *pcm))
                cmd = [AUDIOCONV, '--wav-mono', '--wav-compress', '1']
                if rate != sr:
                    cmd += ['--wav-resample', str(rate)]
                if repeat:
                    cmd += ['--wav-loop', 'true']
                cmd += ['-o', a.out, wav]
                r = subprocess.run(cmd, capture_output=True, text=True)
                if r.returncode != 0:
                    sys.exit(f'{name}: audioconv64 falhou:\n{r.stdout}\n{r.stderr}')
                outp = os.path.join(a.out, name + '.wav64')
                tot_in += blocks * 16
                tot_out += os.path.getsize(outp)
                nconv += 1
                print(f'{name}: sr={sr} -> {rate} loop={int(repeat)} adpcm={blocks * 16} wav64={os.path.getsize(outp)}')
    print(f'TOTAL: {nconv} amostras, ADPCM PS1 {tot_in} B -> WAV64 {tot_out} B')


if __name__ == '__main__':
    main()

