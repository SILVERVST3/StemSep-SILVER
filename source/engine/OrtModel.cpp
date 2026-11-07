#include "OrtModel.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>

#define ORT_API_MANUAL_INIT 1
#include "onnxruntime_c_api.h"

#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#else
 #include <dlfcn.h>
#endif

namespace stemsep
{

namespace
{
    // Equivalente minimo do OrtDmlApi (dml_provider_factory.h), para nao depender de DirectML.h.
    struct DmlApiMinimal
    {
        OrtStatusPtr (ORT_API_CALL* SessionOptionsAppendExecutionProvider_DML) (OrtSessionOptions* options, int device_id) NO_EXCEPTION;
    };

    using GetApiBaseFn = const OrtApiBase* (ORT_API_CALL*) ();

#if defined(_WIN32)
    std::wstring toWide (const std::string& s)
    {
        if (s.empty()) return {};
        const int n = MultiByteToWideChar (CP_UTF8, 0, s.data(), (int) s.size(), nullptr, 0);
        std::wstring w ((size_t) n, L'\0');
        MultiByteToWideChar (CP_UTF8, 0, s.data(), (int) s.size(), w.data(), n);
        return w;
    }
#endif
}

struct OrtModel::Impl
{
#if defined(_WIN32)
    HMODULE lib = nullptr;
#else
    void* lib = nullptr;
#endif
    const OrtApi* api = nullptr;
    OrtEnv* env = nullptr;
    OrtSessionOptions* options = nullptr;
    OrtSession* session = nullptr;
    OrtMemoryInfo* memInfo = nullptr;
    std::string device = "CPU";

    std::mutex runMutex;                 // protege currentRun
    OrtRunOptions* currentRun = nullptr;
    std::atomic<bool> abortRequested { false };

    ~Impl() { release(); }

    void release()
    {
        if (api != nullptr)
        {
            if (session)  { api->ReleaseSession (session); session = nullptr; }
            if (options)  { api->ReleaseSessionOptions (options); options = nullptr; }
            if (memInfo)  { api->ReleaseMemoryInfo (memInfo); memInfo = nullptr; }
            if (env)      { api->ReleaseEnv (env); env = nullptr; }
        }
        // A DLL do runtime fica carregada ate o fim do processo de proposito: descarregar
        // onnxruntime.dll/DirectML.dll com threads internas ainda vivas pode travar o host.
    }

    bool check (OrtStatus* status, std::string& error, const char* what)
    {
        if (status == nullptr)
            return true;
        error = std::string (what) + ": " + api->GetErrorMessage (status);
        api->ReleaseStatus (status);
        return false;
    }

    bool loadLibrary (const std::string& path, std::string& error)
    {
        if (api != nullptr)
            return true;
#if defined(_WIN32)
        const std::wstring w = toWide (path);
        lib = LoadLibraryExW (w.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (lib == nullptr)
        {
            error = "nao foi possivel carregar o ONNX Runtime (" + path + "), erro Windows "
                  + std::to_string ((unsigned long) GetLastError());
            return false;
        }
        auto fn = reinterpret_cast<GetApiBaseFn> (reinterpret_cast<void*> (GetProcAddress (lib, "OrtGetApiBase")));
#else
        lib = dlopen (path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (lib == nullptr)
        {
            const char* e = dlerror();
            error = "nao foi possivel carregar o ONNX Runtime (" + path + "): " + (e ? e : "?");
            return false;
        }
        auto fn = reinterpret_cast<GetApiBaseFn> (dlsym (lib, "OrtGetApiBase"));
#endif
        if (fn == nullptr)
        {
            error = "OrtGetApiBase nao encontrado em " + path;
            return false;
        }
        const OrtApiBase* base = fn();
        api = base != nullptr ? base->GetApi (ORT_API_VERSION) : nullptr;
        if (api == nullptr)
        {
            error = std::string ("versao do ONNX Runtime incompativel (precisa da API ")
                  + std::to_string (ORT_API_VERSION) + ", DLL tem " + (base ? base->GetVersionString() : "?") + ")";
            return false;
        }
        return true;
    }

    bool createSession (const std::string& modelPath, bool useGpu, int numThreads, std::string& error)
    {
        if (options) { api->ReleaseSessionOptions (options); options = nullptr; }
        if (session) { api->ReleaseSession (session); session = nullptr; }

        if (! check (api->CreateSessionOptions (&options), error, "CreateSessionOptions")) return false;
        if (! check (api->SetSessionGraphOptimizationLevel (options, ORT_ENABLE_ALL), error, "GraphOptimization")) return false;
        if (! check (api->SetInterOpNumThreads (options, 1), error, "InterOpThreads")) return false;
        if (! check (api->SetIntraOpNumThreads (options, numThreads), error, "IntraOpThreads")) return false;
        // nao deixar as threads do ORT "girando" (libera CPU para o DAW)
        check (api->AddSessionConfigEntry (options, "session.intra_op.allow_spinning", "0"), error, "allow_spinning");
        // devolve a memoria de trabalho (~2-3 GB por bloco) ao sistema entre um bloco e outro
        if (! useGpu)
            check (api->DisableCpuMemArena (options), error, "DisableCpuMemArena");
        error.clear();

        if (useGpu)
        {
            // Requisitos do DirectML EP
            if (! check (api->DisableMemPattern (options), error, "DisableMemPattern")) return false;
            if (! check (api->SetSessionExecutionMode (options, ORT_SEQUENTIAL), error, "ExecutionMode")) return false;
            const void* dmlPtr = nullptr;
            OrtStatus* st = api->GetExecutionProviderApi ("DML", ORT_API_VERSION, &dmlPtr);
            if (st != nullptr || dmlPtr == nullptr)
            {
                if (st) check (st, error, "DirectML");
                else error = "DirectML indisponivel nesta DLL do ONNX Runtime";
                return false;
            }
            auto* dml = static_cast<const DmlApiMinimal*> (dmlPtr);
            if (! check (dml->SessionOptionsAppendExecutionProvider_DML (options, 0), error, "DirectML")) return false;
        }

#if defined(_WIN32)
        const std::wstring wpath = toWide (modelPath);
        if (! check (api->CreateSession (env, wpath.c_str(), options, &session), error, "CreateSession")) return false;
#else
        if (! check (api->CreateSession (env, modelPath.c_str(), options, &session), error, "CreateSession")) return false;
#endif
        device = useGpu ? "GPU (DirectML)" : ("CPU (" + std::to_string (numThreads) + " threads)");
        return true;
    }
};

OrtModel::OrtModel() : impl (std::make_unique<Impl>()) {}
OrtModel::~OrtModel() = default;

bool OrtModel::isLoaded() const noexcept { return impl->session != nullptr; }
std::string OrtModel::deviceDescription() const { return impl->device; }

bool OrtModel::load (const std::string& runtimeLibPath, const std::string& modelPath,
                     Device device, int numThreads, std::string& error)
{
    if (! impl->loadLibrary (runtimeLibPath, error))
        return false;

    auto* api = impl->api;
    if (impl->env == nullptr)
        if (! impl->check (api->CreateEnv (ORT_LOGGING_LEVEL_WARNING, "StemSepSILVER", &impl->env), error, "CreateEnv"))
            return false;
    if (impl->memInfo == nullptr)
        if (! impl->check (api->CreateCpuMemoryInfo (OrtArenaAllocator, OrtMemTypeDefault, &impl->memInfo), error, "MemoryInfo"))
            return false;

    if (numThreads <= 0)
    {
        const unsigned hc = std::max (1u, std::thread::hardware_concurrency());
        numThreads = (int) std::max (1u, hc >= 4 ? hc / 2 : hc);   // ~ nucleos fisicos
    }

    if (device == Device::Gpu || device == Device::Auto)
    {
        std::string gpuError;
        if (impl->createSession (modelPath, true, numThreads, gpuError))
            return true;
        if (device == Device::Gpu)
        {
            error = gpuError;
            return false;
        }
        // Auto: cai para CPU
    }
    return impl->createSession (modelPath, false, numThreads, error);
}

void OrtModel::abortRun()
{
    impl->abortRequested = true;
    const std::lock_guard<std::mutex> l (impl->runMutex);
    if (impl->currentRun != nullptr && impl->api != nullptr)
        if (OrtStatus* st = impl->api->RunOptionsSetTerminate (impl->currentRun))
            impl->api->ReleaseStatus (st);
}

void OrtModel::resetAbort()
{
    impl->abortRequested = false;
}

bool OrtModel::run (const float* input, float* output, std::string& error)
{
    if (impl->session == nullptr)
    {
        error = "modelo nao carregado";
        return false;
    }
    auto* api = impl->api;
    const int64_t inShape[4]  { 1, kInChannels,  kFreqs, kFrames };
    const int64_t outShape[4] { 1, kOutChannels, kFreqs, kFrames };

    OrtValue* in = nullptr;
    OrtValue* out = nullptr;
    if (! impl->check (api->CreateTensorWithDataAsOrtValue (impl->memInfo, const_cast<float*> (input),
                                                            inputSize() * sizeof (float), inShape, 4,
                                                            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &in), error, "input"))
        return false;
    if (! impl->check (api->CreateTensorWithDataAsOrtValue (impl->memInfo, output,
                                                            outputSize() * sizeof (float), outShape, 4,
                                                            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &out), error, "output"))
    {
        api->ReleaseValue (in);
        return false;
    }

    const char* inNames[]  { "spec" };
    const char* outNames[] { "stems" };
    const OrtValue* inputs[] { in };
    OrtValue* outputs[] { out };

    OrtRunOptions* runOptions = nullptr;
    if (! impl->check (api->CreateRunOptions (&runOptions), error, "RunOptions"))
    {
        api->ReleaseValue (out);
        api->ReleaseValue (in);
        return false;
    }
    {
        const std::lock_guard<std::mutex> l (impl->runMutex);
        impl->currentRun = runOptions;
        if (impl->abortRequested.load())
            if (OrtStatus* st = api->RunOptionsSetTerminate (runOptions))
                api->ReleaseStatus (st);
    }
    const bool ok = impl->check (api->Run (impl->session, runOptions, inNames, inputs, 1, outNames, 1, outputs), error, "Run");
    {
        const std::lock_guard<std::mutex> l (impl->runMutex);
        impl->currentRun = nullptr;
    }
    api->ReleaseRunOptions (runOptions);

    // Se o ORT substituiu o OrtValue de saida (nao deveria), copia o conteudo.
    if (ok && outputs[0] != out && outputs[0] != nullptr)
    {
        float* data = nullptr;
        if (impl->check (api->GetTensorMutableData (outputs[0], (void**) &data), error, "output data") && data)
            std::memcpy (output, data, outputSize() * sizeof (float));
        api->ReleaseValue (outputs[0]);
    }
    api->ReleaseValue (out);
    api->ReleaseValue (in);
    return ok;
}

} // namespace stemsep
