#pragma once
// Reamostragem de alta qualidade (r8brain-free-src, "Sample rate converter designed by
// Aleksey Vaneev of Voxengo"). Usada so quando o audio nao esta em 44.1 kHz.

#include <cstdint>
#include <vector>

namespace stemsep
{

// Converte 'in' de srcRate para dstRate. outLength = numero exato de amostras desejado.
void resampleChannel (const float* in, int64_t inLength, double srcRate,
                      float* out, int64_t outLength, double dstRate);

inline int64_t resampledLength (int64_t inLength, double srcRate, double dstRate)
{
    return (int64_t) ((double) inLength * dstRate / srcRate + 0.5);
}

} // namespace stemsep
