#pragma once
// Consistencia de mixagem: garante que  bateria + baixo + vocal + instrumentos == mix original.
//
// A rede neural nunca devolve stems que somam exatamente o mix. O que sobra (residuo
// r = mix - soma) e redistribuido entre os stems por mascaras suaves tempo-frequencia
// (Wiener: M_i = |S_i|^2 / soma |S_j|^2), numa STFT Hann 2048/512 com reconstrucao perfeita.
// Como soma(M_i) = 1 em cada bin e a STFT e linear, a soma final e o mix bit a bit
// (erro de ponto flutuante, ~ -140 dB). O ultimo stem (instrumentos) recebe o que faltar
// do arredondamento, entao a nulidade e exata.

#include <array>
#include <cstdint>
#include <vector>

#include "StemTypes.h"

namespace stemsep
{

// mix: canal unico; stems[i]: canal unico correspondente (sao modificados no lugar).
void applyMixConsistency (const float* mix, int64_t length, std::array<float*, kNumStems> stems);

// Teste de nulidade: mix + (-1) * soma(stems)
NullReport measureNull (const AudioData& mix, const std::array<AudioData, kNumStems>& stems);

} // namespace stemsep
