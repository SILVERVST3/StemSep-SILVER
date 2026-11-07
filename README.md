# StemSep SILVER

Plugin **VST3 com ARA2** para separar uma música em 4 stems, no estilo do Moises:
**Bateria · Baixo · Vocal · Instrumentos**. Funciona dentro do REAPER (Windows), sem internet:
a separação roda no seu PC (CPU ou placa de vídeo via DirectML).

## Como funciona

| Etapa | O que faz |
|---|---|
| Modelo | **SCNet XL IHF** (projeto Music-Source-Separation-Training, licença MIT). SDR médio de 10,08 dB no teste do MUSDB18-HQ, acima do HTDemucs 4 usado em muitos apps. |
| Inferência | ONNX Runtime 1.22 em C++. Usa a GPU por DirectML (Radeon RX 580 incluída) e passa para a CPU sozinho se a GPU falhar. |
| Blocos | Trechos de 11 s (o tamanho de treino do modelo), sobreposição 4x e crossfade de 1,1 s. Não aparecem emendas nem cliques entre os blocos. |
| STFT | Feita em C++ e conferida contra o `torch.stft` original: diferença de 1,5·10⁻⁷. |
| Taxa de amostragem | O modelo trabalha em 44,1 kHz. Áudio em 48 kHz ou outra taxa é convertido pelo r8brain (erro abaixo de −150 dB) e volta para a taxa original. |
| Consistência de mixagem | A sobra da rede neural (o que nenhum stem pegou) é redistribuída por máscaras de Wiener. Assim **Bateria + Baixo + Vocal + Instrumentos = original**, e o teste de inversão de polaridade dá silêncio total. |
| Reprodução | Volume, mute e solo usam rampa de ganho por bloco, então mexer nos controles durante a reprodução não gera clique. |

## Instalação

1. No GitHub, abra **Actions → Build StemSep SILVER → artefato `StemSep-SILVER-VST3-Windows`** e extraia o zip.
2. Copie a pasta inteira **`StemSep SILVER.vst3`** para `C:\Program Files\Common Files\VST3\`.
   Dentro dela, em `Contents\Resources`, ficam o modelo (`StemSep_SCNet_XL.onnx`, cerca de 216 MB),
   o `onnxruntime.dll` e o `DirectML.dll`. Não separe esses arquivos da pasta.
3. No REAPER: *Options → Preferences → Plug-ins → VST → Re-scan*.

## Uso no REAPER (modo ARA)

1. Coloque a música numa faixa e insira **StemSep SILVER** direto no FX da faixa (do mesmo jeito que funcionou com o VX_Leveler).
2. O plugin lê o item e começa a separar em segundo plano. Enquanto isso, você ouve o original, e a barra mostra o progresso e o tempo restante.
3. Quando terminar, o plugin passa a tocar os stems no lugar do original. Cada stem tem fader (−∞ a +12 dB), **M** (mute) e **S** (solo).
4. **Exportar stems…** grava os 4 WAV numa pasta. Você também pode arrastar cada stem direto para a timeline pela alça **⇩ Arrastar p/ DAW** (o arquivo sai em WAV 32 bits float).
5. O resultado fica em cache (`%APPDATA%\StemSep SILVER\Cache`). Ao reabrir o projeto, os stems voltam na hora, sem separar de novo. O botão **Limpar cache** apaga esses arquivos.

Num host sem ARA, o plugin entra em **modo arquivo**: abra ou arraste um áudio para a janela do plugin. Os stems tocam alinhados ao início do projeto e podem ser exportados do mesmo jeito.

## Qualidade × tempo

| Qualidade | O que muda | Uso sugerido |
|---|---|---|
| Rápida | sobreposição 2x | prévia |
| **Alta (padrão)** | sobreposição 4x, igual à avaliação oficial do modelo | uso normal |
| Máxima | 4x + TTA: média de 3 passadas (normal, canais trocados, polaridade invertida) | menos artefatos, 3x mais lenta |

Estimativa para uma música de 4 minutos, sem contar o cache: na **GPU RX 580**, de 3 a 6 min na qualidade Alta.
Na **CPU i5-10400**, de 12 a 20 min na Alta e de 6 a 10 min na Rápida.
Essas faixas são estimativas: o plugin mostra o tempo real restante enquanto trabalha.

## Teste de nulidade (inversão de polaridade)

- **No plugin:** depois de separar, o painel mostra `Nulidade: −14x dBFS ✓`. O botão **Teste de nulidade**
  soma os 4 stems com a polaridade invertida do original e mostra o resíduo. Abaixo de −100 dBFS, o cancelamento é total.
- **No REAPER, à mão:** importe os 4 WAV exportados e o original em faixas separadas, inverta a polaridade (ø)
  só da faixa do original e toque tudo junto. O medidor do master fica em silêncio (−∞ / abaixo de −140 dB).

Os resultados dos testes feitos durante o desenvolvimento (nulidade, fidelidade ao PyTorch, SDR em músicas reais,
emendas entre blocos) estão em [TESTES.md](TESTES.md).

> O teste de nulidade prova que nada se perdeu nem foi inventado, ou seja, que a soma dos stems é exatamente o original.
> Ele não mede, sozinho, o quanto cada stem está limpo. Isso depende do modelo (veja os números de SDR acima).

## Compilar (GitHub Actions)

Envie a pasta para um repositório no GitHub. O workflow `.github/workflows/build-windows.yml` faz o resto:

1. baixa o checkpoint do SCNet XL IHF e o exporta para ONNX (`tools/export_scnet_onnx.py --verify` confere o ONNX contra o PyTorch);
2. baixa o JUCE 8.0.9, o ARA SDK 2.2.0, o ONNX Runtime 1.22 (DirectML) e o DirectML 1.15.4;
3. compila o VST3 com ARA e a ferramenta de teste `stemsep_cli.exe`;
4. roda um **teste de fumaça**: separa uma música sintética em 48 kHz e só aprova o build se a soma dos stems,
   com a polaridade invertida, cancelar o original;
5. publica o artefato com a pasta `StemSep SILVER.vst3` pronta.

O modelo exportado fica em cache no GitHub, então os builds seguintes são mais rápidos.

## Estrutura

```
source/engine/   motor de separação sem JUCE (STFT, ONNX Runtime, blocos, reamostragem, consistência)
source/plugin/   plugin JUCE: ARA, reprodução, serviço em segundo plano, cache, interface SILVER
tools/           exportação do modelo, referência em Python, CLI de teste, gerador de áudio de teste
third_party/     pocketfft (BSD), r8brain-free-src (MIT), headers do ONNX Runtime (MIT)
```

## Créditos

- SCNet: Tong et al., *SCNet: Sparse Compression Network for Music Source Separation* (ICASSP 2024).
  Pesos XL IHF de Roman Solovyev (ZFTurbo), Music-Source-Separation-Training, licença MIT.
- ONNX Runtime e DirectML: Microsoft (MIT / redistribuível).
- Sample rate converter designed by Aleksey Vaneev of Voxengo (r8brain-free-src, MIT).
- pocketfft: Max-Planck-Society (BSD-3).
- JUCE 8 e ARA SDK 2.2 (Celemony).
