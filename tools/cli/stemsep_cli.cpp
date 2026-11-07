// StemSep SILVER - linha de comando para testar o motor (mesmo codigo do plugin).
//
//   stemsep_cli --runtime libonnxruntime.so --model StemSep_SCNet_XL.onnx --input mix.wav
//               --outdir out [--overlap 2|4] [--tta] [--device auto|cpu|gpu] [--threads N]
//               [--no-consistency]
//
// Grava Bateria.wav, Baixo.wav, Vocal.wav, Instrumentos.wav (float 32 bits) e imprime o
// teste de nulidade (mix + polaridade invertida da soma dos stems).

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iterator>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "../../source/engine/Separator.h"

using namespace stemsep;

namespace
{
    template <typename T> T readLE (const uint8_t* p) { T v; std::memcpy (&v, p, sizeof (T)); return v; }

    bool readWav (const std::string& path, AudioData& out, std::string& err)
    {
        std::ifstream f (path, std::ios::binary);
        if (! f) { err = "nao abriu " + path; return false; }
        std::vector<uint8_t> d ((std::istreambuf_iterator<char> (f)), {});
        if (d.size() < 44 || std::memcmp (d.data(), "RIFF", 4) || std::memcmp (d.data() + 8, "WAVE", 4))
        { err = "nao e WAV"; return false; }
        size_t pos = 12;
        int fmt = 0, ch = 0, bits = 0; uint32_t sr = 0;
        const uint8_t* data = nullptr; size_t dataLen = 0;
        while (pos + 8 <= d.size())
        {
            const uint32_t len = readLE<uint32_t> (&d[pos + 4]);
            if (! std::memcmp (&d[pos], "fmt ", 4))
            {
                fmt = readLE<uint16_t> (&d[pos + 8]);
                ch = readLE<uint16_t> (&d[pos + 10]);
                sr = readLE<uint32_t> (&d[pos + 12]);
                bits = readLE<uint16_t> (&d[pos + 22]);
                if (fmt == 0xFFFE && len >= 40) fmt = readLE<uint16_t> (&d[pos + 32]);
            }
            else if (! std::memcmp (&d[pos], "data", 4))
            {
                data = &d[pos + 8];
                dataLen = std::min<size_t> (len, d.size() - pos - 8);
            }
            pos += 8 + len + (len & 1);
        }
        if (! data || ch < 1) { err = "WAV sem dados"; return false; }
        const int bps = bits / 8;
        const size_t frames = dataLen / (size_t) (bps * ch);
        out.sampleRate = sr;
        out.resize (ch, (int64_t) frames);
        for (size_t i = 0; i < frames; ++i)
            for (int c = 0; c < ch; ++c)
            {
                const uint8_t* p = data + (i * ch + c) * bps;
                float v = 0;
                if (fmt == 3 && bits == 32) v = readLE<float> (p);
                else if (fmt == 3 && bits == 64) v = (float) readLE<double> (p);
                else if (bits == 16) v = readLE<int16_t> (p) / 32768.0f;
                else if (bits == 24) { int32_t x = (p[0] | (p[1] << 8) | (p[2] << 16)); if (x & 0x800000) x |= ~0xFFFFFF; v = x / 8388608.0f; }
                else if (bits == 32) v = (float) (readLE<int32_t> (p) / 2147483648.0);
                else { err = "formato WAV nao suportado"; return false; }
                out.channels[(size_t) c][i] = v;
            }
        return true;
    }

    bool writeWavFloat (const std::string& path, const AudioData& a)
    {
        std::ofstream f (path, std::ios::binary);
        if (! f) return false;
        const uint16_t ch = (uint16_t) a.numChannels();
        const uint32_t sr = (uint32_t) std::lround (a.sampleRate);
        const uint32_t frames = (uint32_t) a.numSamples();
        const uint32_t dataLen = frames * ch * 4;
        auto w32 = [&] (uint32_t v) { f.write ((const char*) &v, 4); };
        auto w16 = [&] (uint16_t v) { f.write ((const char*) &v, 2); };
        f.write ("RIFF", 4); w32 (36 + dataLen); f.write ("WAVE", 4);
        f.write ("fmt ", 4); w32 (16); w16 (3); w16 (ch); w32 (sr); w32 (sr * ch * 4); w16 ((uint16_t) (ch * 4)); w16 (32);
        f.write ("data", 4); w32 (dataLen);
        std::vector<float> inter ((size_t) frames * ch);
        for (uint32_t i = 0; i < frames; ++i)
            for (uint16_t c = 0; c < ch; ++c)
                inter[(size_t) i * ch + c] = a.channels[c][i];
        f.write ((const char*) inter.data(), (std::streamsize) (inter.size() * 4));
        return (bool) f;
    }
}

int main (int argc, char** argv)
{
    std::string runtime, modelPath, input, outdir = ".";
    SeparationOptions opts;
    Device device = Device::Cpu;
    int threads = 0;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&] () -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--runtime") runtime = next();
        else if (a == "--model") modelPath = next();
        else if (a == "--input") input = next();
        else if (a == "--outdir") outdir = next();
        else if (a == "--overlap") opts.overlap = std::stoi (next());
        else if (a == "--tta") opts.tta = true;
        else if (a == "--no-consistency") opts.mixConsistency = false;
        else if (a == "--threads") threads = std::stoi (next());
        else if (a == "--device")
        {
            const std::string d = next();
            device = d == "gpu" ? Device::Gpu : d == "auto" ? Device::Auto : Device::Cpu;
        }
    }
    if (runtime.empty() || modelPath.empty() || input.empty())
    {
        std::cerr << "uso: stemsep_cli --runtime <onnxruntime> --model <onnx> --input <wav> --outdir <dir>\n";
        return 2;
    }

    std::string err;
    AudioData mix;
    if (! readWav (input, mix, err)) { std::cerr << err << "\n"; return 1; }
    std::cout << "entrada: " << mix.numChannels() << " canais, " << mix.sampleRate << " Hz, "
              << (double) mix.numSamples() / mix.sampleRate << " s\n";

    Separator sep;
    if (! sep.prepare (runtime, modelPath, device, threads, err)) { std::cerr << "prepare: " << err << "\n"; return 1; }
    std::cout << "dispositivo: " << sep.deviceDescription() << "\n";
    std::cout << "passadas do modelo: " << Separator::countChunks (mix.numSamples(), mix.sampleRate, opts) << "\n";

    SeparationResult res;
    int lastPct = -1;
    auto prog = [&] (double f, const std::string& s)
    {
        const int pct = (int) (f * 100.0);
        if (pct != lastPct) { lastPct = pct; std::cout << "  " << pct << "% " << s << "\n" << std::flush; }
    };
    if (! sep.separate (mix, opts, res, prog, {}, err)) { std::cerr << "separate: " << err << "\n"; return 1; }

    for (int s = 0; s < kNumStems; ++s)
    {
        const std::string p = outdir + "/" + stemFileName (s) + ".wav";
        if (! writeWavFloat (p, res.stems[(size_t) s])) { std::cerr << "falha ao gravar " << p << "\n"; return 1; }
    }
    std::printf ("tempo: %.1f s (%.2fx tempo real)\n", res.secondsElapsed,
                 res.secondsElapsed / ((double) mix.numSamples() / mix.sampleRate));
    std::printf ("NULIDADE: residuo pico = %.1f dBFS, energia relativa = %.1f dB\n",
                 res.nullTest.residualPeakDbfs, res.nullTest.residualRelDb);
    if (opts.mixConsistency && res.nullTest.residualPeakDbfs > -100.0)
    {
        std::printf ("FALHA: a soma dos stems nao cancelou o original\n");
        return 3;
    }
    std::printf ("OK: soma dos stems com polaridade invertida cancela o original\n");
    return 0;
}
