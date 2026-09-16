# Enhancer 0.17 (dsp_enh.dll) — Especificação do algoritmo recuperada por engenharia reversa

Fonte: `dsp_enh.dll` (Ioscasoft, 20/out/2001, x86, VC6), analisada com Ghidra 12.1.3 (estática) e com um
host de 32 bits que carrega a DLL original e mede a saída (dinâmica). Todos os coeficientes abaixo foram
conferidos contra a memória do engine em tempo de execução a 44 100 Hz.

Arquivos de apoio (pasta `C:\Users\Josias\Tools\enhancer_re`):
- `out/decompiled.c`, `out/listing.asm`, `out/symbols.txt` — saída do Ghidra (247 funções).
- `host/host3.c` + `host/build3.bat` — host de teste sem CRT (resolve kernel32 via PEB) que carrega a DLL,
  configura os sliders e processa `host/in_*.raw` gerando `host/out/<cfg>_<sinal>.raw` (int16 mono 44,1 kHz).
- `host/out/memdump.bin` — dump das regiões de coeficientes com todos os sliders em 50.

Endereços úteis (base 0x10000000): engine `0x101c4748`; flag Power `0x101c4750`;
núcleo DSP `FUN_10001430`; coeficientes `FUN_10002970` + `FUN_10002c40`; reverb init `FUN_10002e60`;
setters `0x10003400..0x100035f0`; tabela do Boost `FUN_10003300`.

## 1. Interface Winamp

`winampDSPGetHeader2` → header v32 → módulo "Enhancer 0.17". `ModifySamples(this, short* s, n, bps, nch, srate)`
só aceita **16 bits**, mono ou estéreo, processa in-place e devolve `n`. Latência fixa: **511 amostras**
(delay do limitador lookahead). Todo o processamento é em `double`, escala de amostra ±32768.

**Validação:** `host/refmodel.py` implementa esta especificação (caminho mono) e reproduz as saídas da DLL original com diferença máxima de 0–1 LSB em todas as configurações testadas (off, hb50, hb100, db50, tr50, amb50, boost).

## 2. Sliders (0..100) → parâmetros internos

| Slider | Parâmetro | Fórmula |
|---|---|---|
| Volume | `vol` (linear) | dB = 0,4·s − 20 → `vol = 10^(dB/20)`; troca de volume transfere o ganho automático (`auto *= vol_old/vol_new`, ou zera se subir) |
| Harm Bass | `hb_gain`, flag on se s>0 | `g(s) = 1 − log10(1 + 9·(100−s)/100)` (s=50 → 0,2596; s=100 → 1) |
| Harm Bass Range | `hb_range` | `1 + 0,02·s` (1..3) |
| Drum Bass | `db_gain`, flag | `2·g(s)` |
| Drum Bass Range | `db_r` | `1 − 0,01·s` (recalcula coeficientes) |
| Dry Signal | `dry` | dB = 0,2·s − 20 → `10^(dB/20)` (s=100 → 0 dB) |
| Harm Treble | `tr_gain`, flag | `g(s)` |
| Harm Treble Range | `t` | `1 − 0,02·s` (−1..+1) → coeficientes FIR e atraso do dry |
| Ambience | `amb`, flag | `rev_dry = 1 − 0,005·s`; `wet = 0,00027·s`; s=0 zera os buffers |
| Ambience Range | `r` | `r = 0,01·s` → tempo de reverb `T = 1 + 3r` s; damping `a = 0,4 + 0,3r` |
| Boost (botão) | flag byte +0x9 | ver §9 |

Presets de fábrica (ordem acima, sem Boost) estão em `Enhancer/017/enhancer.set`; "Normal" = 72,31,50,30,50,100,50,50,0,50.

## 3. Cadeia de processamento (por amostra, caminho mono)

```
x  ──DC-block──► xd ──┬──[HarmBass on?]──► HPF60(xd) = xh   (senão xh = x bruto)
                      │                     └► LP1►LP2 ► sat() ► LP3►LP4 ► bassH
                      ├──[DrumBass on?]  DB1(xh) − DB2(DB1)  ► bassD   (usa xh, a saída do HPF)
                      ├──[Treble on?]    FIR4(xh) ► sin() ► treb ; xh atrasado 1 ou 2 amostras = xdel
                      └─ sig = xdel·dry + treb
[Ambience on?] sig = rev_dry·sig + wet·Reverb(sig)
y = (sig + bassH + bassD) · vol · auto
auto-gain (§9) → limitador lookahead 511 (§9) → [Boost §9] → clip int16
```

Atenção, comportamento real da 0.17: quando Harm Bass está ligado, o sinal "dry" que segue para
treble/drum/mix passa antes por um **passa-altas de 60 Hz** (sub-graves são removidos do dry e
substituídos pelos graves sintetizados). Com Harm Bass desligado o dry é o sinal bruto (sem DC-block).

## 4. Filtros de entrada

- DC-block: `xd = c·(x − x_prev + xd_prev)`, `c = 1/(π·10/fs + 1)` (44,1k: 0,999288).
- HPF 60 Hz (RBJ, Q = 0,95, ganho +1 dB): `w = 2π·60/fs`, `α = sin(w)·0,5263`, `a0 = 1/(1+α)`,
  `a1 = −2cos(w)·a0`, `a2 = (1−α)·a0`, `b1 = −(1+cos w)·10^0,05·a0`, `b0 = b2 = −b1/2`.
  `xh = b0·(xd + xd2) + b1·xd1 − a1·xh1 − a2·xh2`.
  (44,1k: a0 0,99552, a1 −1,99097, a2 0,99104, b0 1,11697, b1 −2,23394)

## 5. Harmonic Bass

Quatro seções passa-baixas ressonantes de 2ª ordem `y = b·x + a1·y1 − a2·y2`, derivadas de um protótipo
RLC discretizado (FUN_10002970). Com `dt = 1/fs`, `k = 2e-6·π²`, `d = (dt + 0,001)/(500·dt)`, `e = 20·dt`:

```
seção(C):  p = 1/(k·C);  den = (e + p)·d + dt
           b = dt/den;  a1 = (d·p + 2e-6·p/dt + 4e-5)/den;  a2 = 2e-6·p/(den·dt)
LP1: C = 62500     LP2: C = 40000     LP4: C = 25600
LP3: p = 1/(0,2·π²), d3 = (dt + 0,0025)/(500·dt), a1 = (d3·p + 5e-6·p/dt + 1e-4)/den, a2 = 5e-6·p/(den·dt)
```
(44,1k: LP1 b 3,0988e-4 a1 1,976655 a2 0,976977; LP2 b 1,9838e-4 a1 1,977077 a2 0,977283;
LP3 b 2,0095e-4 a1 1,989717 a2 0,989926; LP4 b 1,2699e-4 a1 1,977347 a2 0,977479)

Cadeia: `l1 = LP1(xd)`, `l2 = LP2(l1)`, `v = hb_range·l2`,
saturação log-log simétrica: `h = sign(v)·G·log10(1 + 9·log10(1 + (9/G)·|v|))`, `G = 150000`,
depois `l3 = LP3(hb_gain·h)`, `bassH = LP4(l3)`.
Medido (seno 40 Hz, 8000 pico, HB 50/50): fundamental +7,6 dB, 3º harmônico −18 dB, 5º −45 dB, sem harmônicos pares.
Em estéreo a entrada do bass é `(xdL + xdR)/2` e bassH é somado igual nos dois canais.

## 6. Drum Bass

Duas seções iguais com `R = 115 − 25·db_r`, `L = 1000 + 300·db_r`:
`p = 1/(k·4R²)`, `d = (L·2e-6 + dt)/(L·dt)`, `den = (e + p)·d + dt`, mesmas fórmulas de b/a1/a2 (versão 2e-6/4e-5).
`s1 = DB1(db_gain·xh)`, `s2 = DB2(s1)`, `bassD = s1 − s2` (banda ressonante, realça transientes).
Medido: em seno contínuo quase nada (+0,4 dB); em burst de 60 Hz sobe ~25% e decai em ~40 ms.

## 7. Harmonic Treble

`t = tr_range`. FIR de 4 taps sobre `xh[n..n−3]`: `c = 1/(2|t| + 2)`, coef = `[−t·c, c, t·c, −c]`
(t = 0 → `0,5·(x[n−1] − x[n−3])`). Waveshaper: `treb = 49152·tr_gain·sin(1,5π·fir/32768)`.
Dry atrasado: `xdel = xh[n−1]` se t ≤ 0, `xh[n−2]` se t > 0 (só quando Treble está ligado; estéreo usa índices 12/13 no histórico).

## 8. Ambience (Schroeder/Moorer)

Cinco combs paralelos + quatro allpass em série, por canal. Comprimentos (amostras, fixos):
L: `[2202, 2529, 2910, 3340, 3840]`; R: `[2862, 3286, 3783, 4342, 4992]` (1,3×).
Cada comb tem dois índices: leitura `rd` (começa em 0) e escrita `wr` (começa em ~L/2: 1116,1253,1484,1659,1946),
ambos avançam 1/amostra com wrap em L. Por comb:
```
st = damp_b·st + damp_a·buf[rd]      # damping 1 polo: damp_a = 0,4+0,3r, damp_b = 1−damp_a
buf[rd] = st·fb + in                 # feedback
buf[wr] += in                        # segunda injeção
soma += st
```
`fb_i = 0,01^(L_i / ((1 + 3r)·fs))` → RT60 = 1+3r segundos (44,1k, r=0,5: 0,9121 0,8998 0,8855 0,8698 0,8518; R: 0,8873 0,8717 0,8538 0,8341 0,8118).
Allpass: comprimentos `[339, 398, 478, 728]`, ganhos `[0,63, 0,57, 0,52, 0,48]`,
`tmp = buf[i]; out = tmp − in; buf[i] = tmp·g + in; in = out`.
Mix mono: `sig = rev_dry·sig + wet·ap`. Estéreo: `L = rev_dry·L + wet·(apL − 0,5·apR)`, `R = rev_dry·R + wet·(apR − 0,5·apL)`.
Medido (Amb 50/50): decaimento ≈ −2,5 dB/100 ms.

## 9. Saída: ganho automático, limitador e Boost

- `y = (sig + bassH + bassD)·vol·auto`. Se `|y| > 29000`: `auto *= 48,3333/|y| + 0,998333`.
  Senão, se `auto < 1`: `auto *= 10^(1/(20·fs))` (recupera +1 dB/s).
- Limitador lookahead: limiar `TH = 32685,7843`. Delay de 511 amostras. Ganho necessário por amostra
  `gn = min(1, TH/|y|)`, mantido numa árvore de mínimos (janela 1024, atualizada só durante 1020 amostras
  após um pico); `avg` = média móvel (510) do mínimo da janela; `out = y[n−511]·avg`.
- Boost (se ligado): `env = 0,9656·env + out` (1 polo, `a = 1/(π·500/fs + 1)`), `v = out − 0,01211·(a·env_prev)`,
  depois tabela `T[v]` gerada por: `y = 1,585·v` (10^(4·0,05)); se `y > 20000`: `y = 32750·sin((y−20000)·φ/(51900−20000) + asin(0,6107))`,
  `φ = π/2 + π·5000/51900 − asin(0,6107)`; simétrica para negativos. (Medido: +3,9 dB e soft-clip.)
- Sem Boost: conversão direta `int16` (ftol).

## 10. Plano de reimplementação (Windows inteiro)

1. Portar §3–§9 para C++ como classe `EnhancerDSP` (float/double, qualquer fs; os comprimentos do reverb
   devem escalar por fs/44100 para manter o timbre, o original não escala).
2. Validar contra `host/out/*.raw` reprocessando `host/in_*.raw` com os mesmos sliders (tolerância ±1 LSB, exceto
   onde x87 80 bits difere).
3. Empacotar como VST3 (JUCE) com os 10 sliders + Power + Boost e os presets do `.set`.
4. Carregar no Equalizer APO (open source) → efeito em todo o sistema; opcionalmente compilar como estágio
   nativo do Equalizer APO para evitar o host VST.
