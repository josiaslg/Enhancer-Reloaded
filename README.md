# Enhancer Reloaded

**English** · [Português (Brasil)](#português-brasil)

Enhancer Reloaded brings the classic Winamp DSP plug-in **Enhancer 0.17** (Adrian Iosca, 2001) back as a
**system-wide Windows audio effect**: harmonic bass, drum bass, harmonic treble, ambience (reverb), the
automatic volume limiter and the Boost mode, applied to every application, not only Winamp.

The original DSP algorithm was recovered by reverse engineering the plug-in (`dsp_enh.dll`) and
reimplemented from scratch; the port reproduces the original output sample-exact. The control panel
reproduces the original look (vector redraw in high definition, or the original pixel skin).

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

- Ten sliders as in the original (Volume, Harmonic Bass/Range, Drum Bass/Range, Dry Signal, Harmonic
  Treble/Range, Ambience/Range). Scale is the original one: Volume 50 = 0 dB (0.4 dB per step), Dry 100 = 0 dB,
  effect sliders at 0 = off. Drag the knob, click on the groove or use the mouse wheel.
- **Power** (LED lit = effect on), **Boost**, **Presets** (11 factory presets, your own presets, save/delete,
  reset), **Help** (original help file, if Winamp is installed), **About**.
- Right-click the panel: minimize, always on top, size (1x–4x), look (high definition / original skin),
  start with Windows, exit.
- Tray icon: open panel, power, start with Windows, uninstall, exit.
- Settings and presets are kept in `EnhancerReloaded.ini` next to the exe (`[State]` = last values, position,
  last preset; `[Presets]` = `name=v1,...,v10`). The last state is restored on the next start.

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
zero; o port reproduz a saída original amostra a amostra. O painel reproduz o visual original (redesenho
vetorial em alta definição ou a skin de pixels original).

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

- Dez sliders como no original (Volume, Harmonic Bass/Range, Drum Bass/Range, Dry Signal, Harmonic
  Treble/Range, Ambience/Range). A escala é a original: Volume 50 = 0 dB (0,4 dB por passo), Dry 100 = 0 dB,
  sliders de efeito em 0 = desligado. Arraste o knob, clique na trilha ou use a roda do mouse.
- **Power** (LED aceso = efeito ligado), **Boost**, **Presets** (11 de fábrica, os seus, salvar/apagar, zerar),
  **Help** (ajuda original, se o Winamp estiver instalado), **About**.
- Botão direito no painel: minimizar, sempre no topo, tamanho (1x–4x), visual (alta definição / skin
  original), iniciar com o Windows, sair.
- Ícone da bandeja: abrir painel, power, iniciar com o Windows, desinstalar, sair.
- Configurações e presets ficam em `EnhancerReloaded.ini` ao lado do exe (`[State]` = últimos valores, posição,
  último preset; `[Presets]` = `nome=v1,...,v10`). O último estado é restaurado na próxima abertura.

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

## Notas para desenvolvedores

- O motor cria os APOs por **agregação COM**: a class factory precisa aceitar `pUnkOuter` e o objeto expõe um
  `IUnknown` não delegante. Sem isso todo stream do endpoint falha (sem som).
- APOs só são carregados se estiverem em `HKCR\AudioEngine\AudioProcessingObjects\{CLSID}`; o
  `DllRegisterServer` chama `RegisterAPO` para isso.
- `IsInputFormatSupported` / `IsOutputFormatSupported` recebem formatos opcionais (NULL); devolva o formato
  oposto quando o pedido vier vazio.
- Em `MMDevices\Audio\Render` os Administradores só têm `SetValue`; abra as chaves exatamente com esse direito.
- Log de depuração (compilando com `/DENHANCER_LOG`): `C:\ProgramData\EnhancerAPO\log.txt`.
