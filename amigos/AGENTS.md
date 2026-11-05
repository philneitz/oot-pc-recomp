# Instruções para agentes de IA

Você está ajudando alguém a compilar *Donkey Kong Bananza* como executável nativo do Windows, usando a branch `local-prs` do fork [sostenesfreitas/dkb-pc-recomp](https://github.com/sostenesfreitas/dkb-pc-recomp), baseada em [dkb-pc-recomp](https://github.com/Lucas-blip-png/dkb-pc-recomp) `6386b42`. O resultado é experimental: pode travar e faltam efeitos.

## Regras
- Trabalhe só com o dump da própria pessoa. Se ela não tiver, pare. Não explique como obter ou extrair o jogo, nem procure jogo, keys ou firmware.
- Nunca envie, publique ou compartilhe `dkb.exe`, `generated\`, `build\`, cache de shaders, saves ou o dump. Isso também vale para issues, PRs e pastes.
- Peça confirmação antes de instalar programas no sistema e antes de postar qualquer coisa no GitHub.
- Leia `AGENTS.md`, `CONTRIBUTING.md` e `docs/building.md` do projeto: se contradisserem este arquivo sobre como compilar, siga o projeto e avise.
- Não diga que funcionou sem verificar. Relate erros com a saída real.

## 1. Verificar antes de começar
- Windows x64, CPU com AVX2, Vulkan 1.3, 16 GB de RAM, cerca de 60 GB livres.
- O dump precisa ter `base\exefs` (`main`, `sdk`, `rtld`, `main.npdm`) e `base\romfs`.
- Só a versão base 1.0.0 serve: compare o SHA-256 dos arquivos de `exefs` com `tools\data\expected-dkb100.json`. A atualização 458752 e o DLC não têm suporte, e um `exefs` de atualização falha nessa checagem.

## 2. Ferramentas
Python 3.12 e Git, mais:
```powershell
winget install --id MSYS2.MSYS2 -e
winget install --id Microsoft.VisualStudio.BuildTools -e --override "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
winget install --id Microsoft.DotNet.SDK.10 -e
```
Os instaladores podem pedir permissão de administrador: avise a pessoa. No shell do MSYS2 (UCRT64), rode `pacman -Syuu --noconfirm` duas vezes e depois instale os pacotes listados em `docs/building.md`.

## 3. Código
```powershell
git clone -b local-prs https://github.com/sostenesfreitas/dkb-pc-recomp
cd dkb-pc-recomp
py -3.12 -m venv .venv
.venv\Scripts\python -m pip install -r tools\requirements.txt
```

## 4. Compilar
Use `C:\msys64\ucrt64\bin` e `C:\Program Files (x86)\Microsoft Visual Studio\Installer` no início do PATH e `PYTHONUTF8=1`. Rode os comandos da seção "Passos" do `amigos/LEIA-ME.md`, nesta ordem. A etapa do código gerado e a do `build_game.py` são as mais pesadas; rode-as em segundo plano.

Para verificar: `build\dkb100\dkb.exe --selftest` deve terminar com `selftest: passed (0 failures)`.

## 5. Rodar
Copie `amigos\totk.ini` para `build\dkb100` e use `amigos\jogar-dk.ps1`, como no `amigos/LEIA-ME.md`. Sem o `totk.ini`, o terreno destrutível fica preto.
- Ajuste `TOTK_GPU_NAME` para parte do nome da placa, se o PC tiver mais de uma.
- Rode de dentro do repositório: o jogo acha o tradutor procurando `build\xlate` nas pastas acima dele.
- Os engasgos da primeira passada por uma área são normais: vêm de shaders e pipelines sendo preparados.

## 6. Se travar
O log é `build\dkb100\logs\totk.log`. Copie antes de reabrir, porque a execução seguinte o renomeia.

`device fault ... instruction pointer (invalid)` em uma placa NVIDIA é um problema conhecido e sem causa definida. Foi visto nas drivers 617.14, 617.42 e 621.01. `TOTK_PIPE_SYNC=1`, `TOTK_PIPE_WAIT=draw` e `TOTK_GPL=0` não resolveram na uma placa NVIDIA.

Para investigar:
1. Rode com `DKB_PAIR_LOG=1`. O log ganha uma linha `stage set:` a cada combinação nova de shaders.
2. Os suspeitos são as últimas linhas antes da queda, não só a última, porque a GPU trabalha atrás do jogo.
3. Teste por metades com `DKB_SKIP_STAGE=<hex>,<hex>`, no máximo 16 valores. Isso faz sumir o que esses shaders desenham, então peça à pessoa para dizer o que mudou na tela.

A lista em `amigos\jogar-dk.ps1` só foi testada em uma placa NVIDIA. Em AMD ou Intel, teste primeiro sem `DKB_SKIP_STAGE`.

## Sobre esta branch
Ela junta os PRs #67, #68 e #72, ainda não aceitos, renumera o patch `0017` do #72 para `0018` (os dois PRs usavam o mesmo número) e adiciona o diagnóstico acima, além desta pasta. Quando eles entrarem no projeto, prefira o código oficial e mantenha só o que ainda fizer falta.
