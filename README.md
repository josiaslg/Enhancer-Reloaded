# Enhancer Reloaded

**English** · [Português (Brasil)](#português-brasil)

Enhancer Reloaded brings the classic Winamp DSP plug-in **Enhancer 0.17** (Adrian Iosca, 2001) back as a
**system-wide Windows audio effect**: harmonic bass, drum bass, harmonic treble, ambience (reverb), the
automatic volume limiter and the Boost mode, applied to every application, not only Winamp.

The original DSP algorithm was recovered by reverse engineering the plug-in (`dsp_enh.dll`) and
reimplemented from scratch; the port reproduces the original output sample-exact. The control panel
offers three templates: the original look (vector redraw in high definition), the original pixel skin,
and a modern mixing-console look with vertical faders and digital readouts.

Author: **josiaslg** — <https://github.com/josiaslg/Enhancer-Reloaded> — <josiaslg@bsd.com.br>
License: **BSD 2-Clause** (see [LICENSE](LICENSE)). The algorithm design and the original skin bitmap belong to
the author of Enhancer 0.17.

## How it works

The effect runs inside the Windows audio engine (`audiodg.exe`) as an **Audio Processing Object (APO)**,
the same mechanism used by Equalizer APO. No driver, no INF, no signing: the APO is a COM DLL registered in
the registry and attached to each audio output ("endpoint") through its `FxProperties`.

`EnhancerReloaded.exe` is a single file that contains everything:

1. On first run it asks to install (one UAC prompt). It copies the APO DLL **and itself** to
   `C:\Program Files\EnhancerReloaded`, creates a Start Menu shortcut, registers the APO, enables unsigned APOs
   in the engine (`DisableProtectedAudioDG`), creates the parameter key `HKLM\SOFTWARE\EnhancerAPO`, attaches
   the effect to every active output, saves a backup of every value it changes, restarts the audio services
   and then starts the installed copy. Settings and presets of the installed copy live in
   `%APPDATA%\EnhancerReloaded\EnhancerReloaded.ini` (an exe run from any other folder keeps its `.ini` next
   to itself). If a newer exe carries a different audio component, it offers to update it.
2. Then it runs as the control panel. **Panel running = effect on.** Closing it switches the effect off and
   exits; minimizing hides it to the tray (purple hat icon) with the effect on.
3. Right-click the hat icon → **Uninstall Enhancer Reloaded…** (or Windows *Apps & Features* → Uninstall)
   restores every backed-up value, unregisters the APO, deletes the installed files and closes. A progress
   window shows each step of an install, update or uninstall.

Parameters travel through the registry: the panel writes `HKLM\SOFTWARE\EnhancerAPO` (writable by Users),
the APO polls it every 300 ms and publishes its automatic gain (`AutoGain`) for the "max" indicator.

## Using the panel

**Sliders.** Ten sliders as in the original (Volume, Harmonic Bass/Range, Drum Bass/Range, Dry Signal,
Harmonic Treble/Range, Ambience/Range). Scale is the original one: Volume 50 = 0 dB (0.4 dB per step),
Dry 100 = 0 dB, effect sliders at 0 = off. Drag the knob, click on the groove or use the mouse wheel.
The red "max" marker (Classic/Original) or the LIM meter (Modern) shows where the automatic gain is
holding the level when Volume is too high.

**Buttons.**
- **Power** – effect on/off (LED lit = on). With Power off the sliders do nothing.
- **Boost** – the original loudness "Boost" stage.
- **Presets** – menu with the 11 factory presets, your own presets, *Save as…*, *Delete* and *Reset*.
  The active preset is checked; touching a slider clears the check.
- **Help** – opens the original help file, if Winamp with Enhancer 0.17 is installed.
- **About** – version, author, license.

**Title bar.** Drag to move. The minimize button hides the panel to the tray with the effect running;
the close button (and *Exit*) powers the effect off and quits.

**Right-click menu (on the panel).**
- *Minimize* – hide to the tray (effect stays on).
- *Always on top* – keep the panel above other windows.
- *Size* – 1x, 1.5x, 2x, 3x, 4x (the Original skin uses whole steps only).
- *Template* – **Classic (high definition)**: the original layout redrawn as vectors, crisp at any size;
  **Original skin (pixels)**: the sprite sheet from the Winamp plug-in; **Modern (mixer)**: a flat dark
  console with one vertical fader per parameter grouped by section, digital readouts (Volume in dB,
  effects 0–100/OFF), a limiter LED meter next to the Volume fader and flat buttons.
- *Stereo width (Haas)* – optional stereo widening, **off by default** and not part of the original
  plug-in. The mid signal delayed by a few milliseconds is added to the left and subtracted from the right
  channel (before the limiter), so the image gets wider while the mono sum stays untouched. *Off*,
  *Subtle* (25 %, 12 ms), *Medium* (50 %, 16 ms), *Wide* (80 %, 20 ms) or *Custom…* (amount 0–100 and delay
  1–40 ms, typed as `50,16`). Independent of the presets; the Modern template shows the state in its title bar.
- *Start with Windows* – adds/removes a Run entry; the panel then starts hidden in the tray with the effect on.
- *Exit (effect off)*.

**Tray icon (purple hat; grey when the effect is off).** Left click opens the panel. Right click:
*Open panel*, *Power (effect on)*, *Template*, *Stereo width (Haas)*, *Start with Windows*,
*Uninstall Enhancer Reloaded…* and *Exit (effect off)*.

**Settings file.** Everything is kept in `EnhancerReloaded.ini` (`%APPDATA%\EnhancerReloaded` when
installed, next to the exe when run portable): `[State]` = slider values, Boost, last preset, Haas
amount/delay, template, size, window position, always-on-top; `[Presets]` = `name=v1,...,v10`. It is saved on
every change, on exit and on Windows shutdown, and restored on the next start exactly as it was left.

**Lifecycle.** Panel running = effect on. Opening the panel powers the effect on with the saved settings;
closing it powers the effect off. Running a newer `EnhancerReloaded.exe` while the installed panel is
open offers to update it.

## Repository layout

```
dist/EnhancerReloaded.exe   ready to run (installs itself on first run)
src/                        all sources (see below)
LICENSE, README.md
```

`src/`:

| File | What it is |
|---|---|
| `EnhancerDSP.h` | The DSP algorithm, clean-room C++ port validated within 1 LSB against the original DLL |
| `EnhancerAPO.cpp/.def` | The Audio Processing Object (COM, MFX, aggregation-aware) → `EnhancerAPO.dll` |
| `EnhancerReloaded.cpp/.rc/.manifest`, `resource.h` | Control panel + tray + self-installer host → `EnhancerReloaded.exe` |
| `Installer.h` | Install / uninstall logic (elevated helper modes `--install` / `--uninstall`) |
| `res/` | Skin bitmap from the original plug-in, icons, `make_icon.py` (renders the hat icon) |
| `tools/` | `test_apo` (drives the DLL like the engine does), `test_dsp` (compares with reference audio), `apodev`, `apometer`, `aporender` (endpoint diagnostics) |
| `scripts/` | Manual PowerShell alternative to the built-in installer (`register.ps1 -All`, `unregister.ps1`) and a plain WinForms panel |
| `validation/refmodel.py` | Python reference model of the algorithm (bit-exact against the original plug-in) |
| `docs/ENHANCER_SPEC.pt-BR.md` | Full technical specification of the recovered algorithm (Portuguese) |
| `build.bat` | Builds everything |

## Building

Requirements:

- **Visual Studio 2022 Build Tools** (or Community) with the **MSVC v143 x64** toolset.
- **Windows 10/11 SDK** (10.0.22621 or newer). It provides `audioenginebaseapo.h`, `audiomediatype.h`,
  `mmdeviceapi.h` and the static library `audiobaseprocessingobject.lib` (used for `RegisterAPO`).
- Nothing else: no third-party libraries. Linked system libraries: `ole32`, `advapi32`, `user32`, `gdi32`,
  `shell32`, plus `audiobaseprocessingobject.lib` and `legacy_stdio_definitions.lib` for the APO.
- Optional: Python 3 to regenerate the icon (`res/make_icon.py`) or run the reference model.

Build:

```bat
cd src
build.bat
```

Outputs go to `src\build\` and the final `EnhancerReloaded.exe` is copied to `dist\`. The DLL is built first
because the exe embeds it as a resource.

## Antivirus false positives

Some antivirus products (Bitdefender's "Advanced Threat Defense" is a known case, detection
`ML:SuspiciousBehavior`) block the installer by **behaviour**, not because of any malicious code. Everything
this program does is in this repository, but the sequence looks like a lot of malware: an unsigned exe copies
itself into Program Files, writes a DLL there, registers a COM component that the Windows audio engine
(`audiodg.exe`) loads, edits the audio device keys, stops and restarts the Windows Audio service, sets
`DisableProtectedAudioDG` (required for any unsigned APO) and, if you ask for it, adds a Run entry. There is no
code-signing certificate for this project, so the heuristic has nothing to trust.

If it happens:

1. Add an exception for `C:\Program Files\EnhancerReloaded` and for the folder where you keep the exe
   (Bitdefender: Protection → Antivirus → Settings → Manage exceptions, including Advanced Threat Defense).
2. If the antivirus already quarantined and you restored the files, **restart Windows** before running the
   installer again. Restored files stay locked until the next boot; the installer then fails with
   "code 4" (cannot write the audio component) and cannot even update `install.log`. In that case the log is
   written to `C:\ProgramData\EnhancerAPO\install.log` instead.
3. Run the exe again: it detects the incomplete installation and repairs it. Your settings in
   `%APPDATA%\EnhancerReloaded\EnhancerReloaded.ini` are kept.
4. If the folder in Program Files cannot be repaired, delete it and run the installer again; the endpoint
   backups (`backup-*.txt`) are recreated from the current Windows settings.

You can also report the file as a false positive to your antivirus vendor, or build the exe yourself from
`src\` (see *Building*) so that the binary comes from your own machine.

## Notes for developers

- The engine creates APOs through **COM aggregation**: the class factory must accept `pUnkOuter` and the
  object exposes a non-delegating `IUnknown`. Without this every stream on the endpoint fails (no sound).
- APOs are only loaded if listed under `HKCR\AudioEngine\AudioProcessingObjects\{CLSID}`; `DllRegisterServer`
  calls `RegisterAPO` for that.
- `IsInputFormatSupported` / `IsOutputFormatSupported` get optional (NULL) formats; return the opposite format
  when the requested one is missing.
- Under `MMDevices\Audio\Render` Administrators only have `SetValue`; open the keys with exactly that right.
- Debug log (when built with `/DENHANCER_LOG`): `C:\ProgramData\EnhancerAPO\log.txt`.

---

# Português (Brasil)

[English](#enhancer-reloaded) · **Português (Brasil)**

O Enhancer Reloaded traz de volta o clássico plug-in DSP do Winamp **Enhancer 0.17** (Adrian Iosca, 2001)
como um **efeito de áudio de sistema no Windows**: graves harmônicos, drum bass, agudos harmônicos,
ambiência (reverb), limitador automático de volume e o modo Boost, aplicados a todos os programas, não só
ao Winamp.

O algoritmo original foi recuperado por engenharia reversa do plug-in (`dsp_enh.dll`) e reimplementado do
zero; o port reproduz a saída original amostra a amostra. O painel oferece três templates: o visual original
(redesenho vetorial em alta definição), a skin de pixels original e um visual moderno de mesa de som com
faders verticais e mostradores digitais.

Autor: **josiaslg** — <https://github.com/josiaslg/Enhancer-Reloaded> — <josiaslg@bsd.com.br>
Licença: **BSD 2-Clause** (veja [LICENSE](LICENSE)). O desenho do algoritmo e o bitmap da skin original pertencem
ao autor do Enhancer 0.17.

## Como funciona

O efeito roda dentro do motor de áudio do Windows (`audiodg.exe`) como um **Audio Processing Object (APO)**,
o mesmo mecanismo do Equalizer APO. Sem driver, sem INF, sem assinatura: o APO é uma DLL COM registrada no
registro e ligada a cada saída de áudio ("endpoint") pelas suas `FxProperties`.

O `EnhancerReloaded.exe` é um arquivo único com tudo dentro:

1. Na primeira execução ele pede para instalar (um prompt de UAC). Copia a DLL do APO **e a si mesmo** para
   `C:\Program Files\EnhancerReloaded`, cria um atalho no Menu Iniciar, registra o APO, habilita APOs sem
   assinatura no motor (`DisableProtectedAudioDG`), cria a chave de parâmetros `HKLM\SOFTWARE\EnhancerAPO`, liga
   o efeito em todas as saídas ativas, guarda backup de cada valor alterado, reinicia os serviços de áudio e
   abre a cópia instalada. As configurações e presets da cópia instalada ficam em
   `%APPDATA%\EnhancerReloaded\EnhancerReloaded.ini` (um exe rodado de outra pasta mantém o `.ini` ao lado
   dele). Se um exe mais novo trouxer um componente de áudio diferente, ele oferece atualizar.
2. Depois roda como painel de controle. **Painel aberto = efeito ligado.** Fechar desliga o efeito e sai;
   minimizar esconde para a bandeja (ícone do chapéu roxo) mantendo o efeito.
3. Botão direito no chapéu → **Uninstall Enhancer Reloaded…** (ou *Aplicativos e Recursos* do Windows →
   Desinstalar) restaura todos os valores dos backups, remove o registro do APO, apaga os arquivos instalados e
   fecha. Uma janela de progresso mostra cada etapa da instalação, atualização ou desinstalação.

Os parâmetros passam pelo registro: o painel grava em `HKLM\SOFTWARE\EnhancerAPO` (gravável por Usuários), o APO
lê a cada 300 ms e publica o ganho automático (`AutoGain`) para o indicador "max".

## Usando o painel

**Sliders.** Dez sliders como no original (Volume, Harmonic Bass/Range, Drum Bass/Range, Dry Signal,
Harmonic Treble/Range, Ambience/Range). A escala é a original: Volume 50 = 0 dB (0,4 dB por passo),
Dry 100 = 0 dB, sliders de efeito em 0 = desligado. Arraste o knob, clique na trilha ou use a roda do
mouse. A marca vermelha "max" (Classic/Original) ou o medidor LIM (Modern) mostra onde o ganho automático
está segurando o nível quando o Volume está alto demais.

**Botões.**
- **Power** – liga/desliga o efeito (LED aceso = ligado). Com o Power desligado os sliders não fazem nada.
- **Boost** – o estágio "Boost" de loudness do original.
- **Presets** – menu com os 11 presets de fábrica, os seus, *Save as…*, *Delete* e *Reset*. O preset ativo
  fica marcado; mexer num slider tira a marca.
- **Help** – abre a ajuda original, se o Winamp com o Enhancer 0.17 estiver instalado.
- **About** – versão, autor, licença.

**Barra de título.** Arraste para mover. O botão de minimizar esconde o painel na bandeja com o efeito
rodando; o botão de fechar (e *Exit*) desliga o efeito e sai.

**Menu do botão direito (no painel).**
- *Minimize* – esconde na bandeja (efeito continua ligado).
- *Always on top* – mantém o painel acima das outras janelas.
- *Size* – 1x, 1,5x, 2x, 3x, 4x (a skin original só usa passos inteiros).
- *Template* – **Classic (high definition)**: o layout original redesenhado em vetores, nítido em qualquer
  tamanho; **Original skin (pixels)**: a skin do plug-in do Winamp; **Modern (mixer)**: mesa de som escura
  e plana com um fader vertical por parâmetro agrupado por seção, mostradores digitais (Volume em dB,
  efeitos 0–100/OFF), medidor de limitador em LEDs ao lado do fader de Volume e botões planos.
- *Stereo width (Haas)* – alargamento estéreo opcional, **desligado por padrão** e que não existe no
  plug-in original. O sinal central (mid) atrasado alguns milissegundos é somado ao canal esquerdo e subtraído
  do direito (antes do limitador): a imagem fica mais larga e a soma mono não muda. *Off*, *Subtle*
  (25 %, 12 ms), *Medium* (50 %, 16 ms), *Wide* (80 %, 20 ms) ou *Custom…* (quantidade 0–100 e atraso
  1–40 ms, digitados como `50,16`). Independente dos presets; o template Modern mostra o estado na barra de título.
- *Start with Windows* – cria/remove a entrada de inicialização; o painel então abre escondido na bandeja com
  o efeito ligado.
- *Exit (effect off)*.

**Ícone da bandeja (chapéu roxo; cinza quando o efeito está desligado).** Clique esquerdo abre o painel.
Clique direito: *Open panel*, *Power (effect on)*, *Template*, *Stereo width (Haas)*, *Start with Windows*,
*Uninstall Enhancer Reloaded…* e *Exit (effect off)*.

**Arquivo de configuração.** Tudo fica em `EnhancerReloaded.ini` (`%APPDATA%\EnhancerReloaded` quando
instalado; ao lado do exe quando rodado portátil): `[State]` = valores dos sliders, Boost, último preset,
quantidade/atraso do Haas, template, tamanho, posição da janela, sempre no topo; `[Presets]` =
`nome=v1,...,v10`. É salvo a cada mudança, ao sair e no desligamento do Windows, e restaurado na próxima
abertura exatamente como foi deixado.

**Ciclo de vida.** Painel rodando = efeito ligado. Abrir o painel liga o efeito com as configurações salvas;
fechar desliga. Executar um `EnhancerReloaded.exe` mais novo com o painel instalado aberto oferece a atualização.

## Estrutura do repositório

```
dist/EnhancerReloaded.exe   pronto para rodar (instala a si mesmo na primeira execução)
src/                        todos os fontes (veja abaixo)
LICENSE, README.md
```

`src/`:

| Arquivo | O que é |
|---|---|
| `EnhancerDSP.h` | O algoritmo DSP, port C++ limpo validado em 1 LSB contra a DLL original |
| `EnhancerAPO.cpp/.def` | O Audio Processing Object (COM, MFX, com agregação) → `EnhancerAPO.dll` |
| `EnhancerReloaded.cpp/.rc/.manifest`, `resource.h` | Painel + bandeja + instalador embutido → `EnhancerReloaded.exe` |
| `Installer.h` | Lógica de instalar / desinstalar (modos elevados `--install` / `--uninstall`) |
| `res/` | Bitmap da skin do plug-in original, ícones, `make_icon.py` (desenha o chapéu) |
| `tools/` | `test_apo` (aciona a DLL como o motor faz), `test_dsp` (compara com áudio de referência), `apodev`, `apometer`, `aporender` (diagnóstico de endpoints) |
| `scripts/` | Alternativa manual em PowerShell ao instalador embutido (`register.ps1 -All`, `unregister.ps1`) e um painel WinForms simples |
| `validation/refmodel.py` | Modelo de referência em Python do algoritmo (idêntico bit a bit ao plug-in original) |
| `docs/ENHANCER_SPEC.pt-BR.md` | Especificação técnica completa do algoritmo recuperado |
| `build.bat` | Compila tudo |

## Compilando

Requisitos:

- **Visual Studio 2022 Build Tools** (ou Community) com o toolset **MSVC v143 x64**.
- **Windows 10/11 SDK** (10.0.22621 ou mais novo). Ele traz `audioenginebaseapo.h`, `audiomediatype.h`,
  `mmdeviceapi.h` e a biblioteca estática `audiobaseprocessingobject.lib` (usada para o `RegisterAPO`).
- Nada mais: nenhuma biblioteca de terceiros. Bibliotecas de sistema linkadas: `ole32`, `advapi32`, `user32`,
  `gdi32`, `shell32`, mais `audiobaseprocessingobject.lib` e `legacy_stdio_definitions.lib` no APO.
- Opcional: Python 3 para regerar o ícone (`res/make_icon.py`) ou rodar o modelo de referência.

Compilar:

```bat
cd src
build.bat
```

A saída vai para `src\build\` e o `EnhancerReloaded.exe` final é copiado para `dist\`. A DLL é compilada
primeiro porque o exe a embute como recurso.

## Falsos positivos de antivírus

Alguns antivírus (o "Advanced Threat Defense" do Bitdefender é um caso conhecido, detecção
`ML:SuspiciousBehavior`) bloqueiam o instalador pelo **comportamento**, não por código malicioso. Tudo o que o
programa faz está neste repositório, mas a sequência parece a de muitos malwares: um exe sem assinatura se
copia para Program Files, grava uma DLL lá, registra um componente COM que o motor de áudio do Windows
(`audiodg.exe`) carrega, edita as chaves dos dispositivos de som, para e reinicia o serviço de Áudio do
Windows, ajusta `DisableProtectedAudioDG` (obrigatório para qualquer APO sem assinatura) e, se você pedir, cria
a entrada de inicialização. O projeto não tem certificado de assinatura de código, então a heurística não tem
em que confiar.

Se acontecer:

1. Adicione exceções para `C:\Program Files\EnhancerReloaded` e para a pasta onde você guarda o exe
   (Bitdefender: Proteção → Antivírus → Configurações → Gerenciar exceções, incluindo o Advanced Threat Defense).
2. Se o antivírus já pôs os arquivos em quarentena e você os restaurou, **reinicie o Windows** antes de rodar o
   instalador de novo. Arquivos restaurados ficam travados até o próximo boot; o instalador então falha com
   "código 4" (não consegue gravar o componente de áudio) e nem consegue atualizar o `install.log`. Nesse caso o
   log é gravado em `C:\ProgramData\EnhancerAPO\install.log`.
3. Execute o exe de novo: ele detecta a instalação incompleta e faz o reparo. Suas configurações em
   `%APPDATA%\EnhancerReloaded\EnhancerReloaded.ini` são mantidas.
4. Se a pasta em Program Files não puder ser reparada, apague-a e rode o instalador de novo; os backups dos
   dispositivos (`backup-*.txt`) são recriados a partir das configurações atuais do Windows.

Você também pode reportar o arquivo como falso positivo ao fabricante do antivírus, ou compilar o exe você
mesmo a partir de `src\` (veja *Compilando*), para que o binário venha da sua própria máquina.

## Notas para desenvolvedores

- O motor cria os APOs por **agregação COM**: a class factory precisa aceitar `pUnkOuter` e o objeto expõe um
  `IUnknown` não delegante. Sem isso todo stream do endpoint falha (sem som).
- APOs só são carregados se estiverem em `HKCR\AudioEngine\AudioProcessingObjects\{CLSID}`; o
  `DllRegisterServer` chama `RegisterAPO` para isso.
- `IsInputFormatSupported` / `IsOutputFormatSupported` recebem formatos opcionais (NULL); devolva o formato
  oposto quando o pedido vier vazio.
- Em `MMDevices\Audio\Render` os Administradores só têm `SetValue`; abra as chaves exatamente com esse direito.
- Log de depuração (compilando com `/DENHANCER_LOG`): `C:\ProgramData\EnhancerAPO\log.txt`.
