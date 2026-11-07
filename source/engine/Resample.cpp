#include "Resample.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "CDSPResampler.h"

namespace stemsep
{

void resampleChannel (const float* in, int64_t inLength, double srcRate,
                      float* out, int64_t outLength, double dstRate)
{
    if (outLength <= 0)
        return;

    if (std::abs (srcRate - dstRate) < 1e-9)
    {
        const int64_t n = std::min (inLength, outLength);
        std::copy (in, in + n, out);
        std::fill (out + n, out + outLength, 0.0f);
        return;
    }

    constexpr int kBlock = 16384;
    r8b::CDSPResampler24 rs (srcRate, dstRate, kBlock, 2.0);

    // processa em blocos com compensacao de latencia (equivalente ao oneshot(), mas aceita int64)
    std::vector<double> buf ((size_t) kBlock);
    int64_t inPos = 0;
    int64_t written = 0;
    while (written < outLength)
    {
        int count = kBlock;
        if (inPos < inLength)
        {
            count = (int) std::min<int64_t> (kBlock, inLength - inPos);
            for (int i = 0; i < count; ++i)
                buf[(size_t) i] = (double) in[inPos + i];
            inPos += count;
        }
        else
        {
            std::fill (buf.begin(), buf.end(), 0.0);
        }

        double* op = nullptr;
        const int produced = rs.process (buf.data(), count, op);
        const int64_t take = std::min<int64_t> (produced, outLength - written);
        for (int64_t i = 0; i < take; ++i)
            out[written + i] = (float) op[i];
        written += take;
    }
}

} // namespace stemsep
