# Relatório de testes — StemSep SILVER

Testes feitos em Linux (2 núcleos de CPU, sem GPU) com o mesmo código que o GitHub Actions compila para Windows.

## 1. Teste de nulidade (inversão de polaridade)

É o teste pedido: original + (soma dos 4 stems com a polaridade invertida). Se o resultado é silêncio, nada se perdeu.

| Cenário | Resíduo de pico | Energia do resíduo |
|---|---|---|
| Motor C++, medley de 41 s (6 músicas reais do MUSDB18), qualidade Alta | **−144,5 dBFS** | **−149,2 dB** |
| **Plugin VST3 em modo ARA** (ARA TestHost do SDK da Celemony: o plugin leu o item, separou em segundo plano e tocou os stems) | **−144,5 dBFS** | **−151,1 dB** |
| Entrada estéreo em 48 kHz (passa pela conversão 48 → 44,1 → 48 kHz, igual ao teste do CI) | **−144,5 dBFS** | **−147,4 dB** |
| Saída bruta da rede neural, sem a consistência de mixagem (para comparação) | −36,5 dBFS | −39,4 dB |

Um resíduo de −144 dBFS corresponde ao arredondamento de ponto flutuante de 32 bits, ou seja, silêncio absoluto.
No teste ARA, o áudio renderizado pelo plugin não foi o original passando direto: ele bate com a soma dos 4 stems do cache, com diferença de 6,7·10⁻⁸.

## 2. Fidelidade da implementação

| Verificação | Resultado |
|---|---|
| Modelo ONNX (STFT fora do grafo, DFT por matrizes) vs SCNet original do PyTorch | diferença máxima de 2·10⁻⁷ |
| ONNX Runtime vs PyTorch, núcleo do modelo | diferença relativa de 4·10⁻⁴ |
| STFT em C++ vs `torch.stft` | diferença de 1,5·10⁻⁷ |
| iSTFT em C++ (ida e volta) | erro de −139 dB |
| Motor C++ completo vs referência em Python (mesmo algoritmo do MSST) | SNR de 121 a 128 dB, ou seja, idênticos |
| Consistência de mixagem em C++ vs reimplementação independente em numpy | SNR de 121 a 129 dB |
| Conversor de taxa r8brain (48 → 44,1 → 48 kHz) | erro de −150 dB |

## 3. Qualidade da separação (SDR, quanto maior melhor)

Seis trechos de 7 s do conjunto de teste do MUSDB18 (músicas que o modelo nunca viu), qualidade Alta:

| Trecho | Bateria | Baixo | Instrumentos | Vocal |
|---|---|---|---|---|
| AM Contra – Heart Peripheral | 11,0 | 3,2 | 0,6 | 13,1 |
| Raft Monk – Tiring | 8,9 | 5,5 | 4,3 | 8,0 |
| Zeno – Signs | 10,0 | 5,7 | 8,5 | 11,6 |
| Skelpolu – Resurrection | 11,4 | 8,3 | −0,1 | 0,9 |
| The Easton Ellises – Falcon 69 | 10,4 | 7,3 | 4,2 | 5,6 |
| Signe Jakobsen – What Have You Done To Me | 1,3 | −1,1 | 6,2 | 13,9 |
| **Média** | **8,8** | **4,8** | **3,9** | **8,9** |

Trechos curtos (7 s, codificados em AAC) dão números menores e mais variáveis que a média oficial em músicas inteiras (10,08 dB).
Baixo e "Instrumentos" são as faixas mais difíceis para qualquer modelo aberto.

Efeito das opções, medido nos primeiros 11 s:

| Opção | SDR médio |
|---|---|
| Rápida (sobreposição 2x) | 6,51 dB |
| Rápida + TTA | 6,52 dB |
| **Alta (4x, padrão)** | **6,63 dB** |
| Consistência de mixagem ligada vs desligada | −0,006 dB, sem efeito audível |

## 4. Artefatos nas emendas dos blocos

Nos 44 pontos de emenda do medley, os saltos entre amostras ficaram iguais ou menores que no restante do áudio.
A razão do percentil 99,9 de |Δx| entre as emendas e o resto foi de 0,66 a 0,98, a mesma faixa dos stems originais.
Não há clique nem degrau entre os blocos.

## 5. Robustez

- Fechar o host no meio de uma separação: a inferência é interrompida (ONNX Runtime terminate) e o processo sai limpo em cerca de 1 s.
- O cache é gravado com os 4 stems e o resultado da nulidade. Ao reabrir, os stems carregam sem separar de novo.
- Memória de trabalho da CPU durante a separação: cerca de 2,4 GB de pico, devolvidos ao sistema entre os blocos.
- Velocidade nesta máquina de teste (2 núcleos): cerca de 30 s por bloco de 11 s.
