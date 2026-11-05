# DK Bananza nativo no Windows (com correções extras)

Esta branch só tem código e instruções. **Não tem jogo, keys nem executável.** Você precisa do seu próprio dump do jogo, versão base 1.0.0.

Experimental: pode travar e alguns efeitos ficam faltando.

Usa um assistente de IA? Peça para ele seguir o `amigos/AGENTS.md`.

## O que precisa
- Windows 10 ou 11, CPU com AVX2, placa com Vulkan 1.3, 16 GB de RAM (o jogo usa cerca de 13 GB), cerca de 60 GB livres.
- MSYS2, Visual Studio Build Tools, .NET SDK 10 e Python 3.12. Siga `docs/building.md` do projeto.

## Passos
```powershell
git clone -b local-prs https://github.com/sostenesfreitas/dkb-pc-recomp
cd dkb-pc-recomp
py -3.12 -m venv .venv
.venv\Scripts\python -m pip install -r tools\requirements.txt
```
Em um terminal com `C:\msys64\ucrt64\bin` no início do PATH, troque `<dump>` pela pasta do seu dump:
```powershell
.venv\Scripts\python tools\build_xlate.py
.venv\Scripts\python recomp\gen.py --exefs <dump>\base\exefs --out generated\dkb100 --modules main,sdk --patches recomp\patches-dkb100.txt
.venv\Scripts\python tools\dkb_expected.py --exefs <dump>\base\exefs --name dkb100 --display 1.0.0
.venv\Scripts\python tools\dkb_pack_romfs.py --dir <dump>\base\romfs --out build\extracted\dkb100\romfs
.venv\Scripts\python tools\build_game.py --version dkb100 --expected tools\data\expected-dkb100.json --exefs <dump>\base\exefs --romfs build\extracted\dkb100\romfs
```

## Jogar
Da pasta do repositório:
```powershell
Copy-Item amigos\totk.ini build\dkb100\totk.ini
powershell -ExecutionPolicy Bypass -File amigos\jogar-dk.ps1
```
A lista de shaders pulados só foi testada numa placa NVIDIA. Em AMD ou Intel, teste antes sem `DKB_SKIP_STAGE`.

Não compartilhe o `dkb.exe`, o código gerado, o cache de shaders nem o dump.
