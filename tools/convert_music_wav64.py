#!/usr/bin/env python3
"""Converte os instrumentos da musica (SOUND/MUSIC.BIN: banco de amostras de cada musica) para WAV64/VADPCM (libdragon).
MUSIC.BIN: tabela de u32 (inicio, inicio_das_amostras) por musica + fim; o banco de amostras tem o mesmo formato de um lump de
VOICES2.BLK (u32 n + n x SAMPLE_DATA{address,length,loop,samplerate} + ADPCM do SPU). Saida: <out>/Mss_Sxxx.wav64
(musica ss, instrumento xxx). O sequenciador de musica (xmplay.c) continua igual; so a fonte do audio muda."""
import argparse, os, struct, subprocess, sys, tempfile, wave
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from convert_sfx_wav64 import decode_sample, AUDIOCONV

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--bin', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--rate', type=int, default=16000)
    a = ap.parse_args()
    data = open(a.bin, 'rb').read()
    first = struct.unpack('<I', data[:4])[0]
    tbl = struct.unpack('<%dI' % (first // 4), data[:first])
    nsongs = (len(tbl) - 1) // 2
    os.makedirs(a.out, exist_ok=True)
    tot_in = tot_out = n = 0
    with tempfile.TemporaryDirectory() as tmp:
        for song in range(nsongs):
            s, e = tbl[2 * song + 1], tbl[2 * song + 2]
            cnt = struct.unpack('<I', data[s:s + 4])[0]
            hdr_end = s + 4 + cnt * 16
            for i in range(cnt):
                addr, length, loop, sr = struct.unpack('<4I', data[s + 4 + i * 16: s + 20 + i * 16])
                pcm, repeat, loop_blk, blocks = decode_sample(data, hdr_end + addr, e)
                rate = min(sr, a.rate) if sr > 0 else a.rate
                name = f'M{song:02d}_S{i:03d}'
                wav = os.path.join(tmp, name + '.wav')
                with wave.open(wav, 'wb') as w:
                    w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
                    w.writeframes(struct.pack('<%dh' % len(pcm), *pcm))
                cmd = [AUDIOCONV, '--wav-mono', '--wav-compress', '1']
                if rate != sr:
                    cmd += ['--wav-resample', str(rate)]
                if repeat:
                    off = int(max(loop_blk, 0) * 28 * rate / sr) if sr > 0 else 0
                    cmd += ['--wav-loop', 'true', '--wav-loop-offset', str(off)]
                cmd += ['-o', a.out, wav]
                r = subprocess.run(cmd, capture_output=True, text=True)
                if r.returncode != 0:
                    # amostras minusculas/constantes quebram o compressor do audioconv64: guarda sem compressao
                    cmd[cmd.index('--wav-compress') + 1] = '0'
                    r = subprocess.run(cmd, capture_output=True, text=True)
                if r.returncode != 0:
                    sys.exit(f'{name}: audioconv64 falhou:\n{r.stdout}\n{r.stderr}')
                outp = os.path.join(a.out, name + '.wav64')
                tot_in += blocks * 16; tot_out += os.path.getsize(outp); n += 1
    print(f'MUSICA: {nsongs} musicas, {n} instrumentos, ADPCM PS1 {tot_in} B -> WAV64 {tot_out} B')

if __name__ == '__main__':
    main()
