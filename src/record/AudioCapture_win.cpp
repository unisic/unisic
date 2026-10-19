// Windows sibling of AudioCapture.cpp. The ffmpeg shipped with Unisic on
// Windows has no input that reads a playback device (no pulse, and dshow only
// lists capture devices), so the endpoint is recorded here through WASAPI: the
// default output in loopback mode for system audio, a capture endpoint for the
// microphone, and process loopback (Windows 10 2004+) for one application's
// process tree. One thread per source owns every COM object and writes raw PCM
// into a named pipe, which ffmpeg opens like a file.
//
// ffmpeg timestamps raw PCM by counting samples, so a gap in the samples is a
// gap in the timeline. Loopback delivers NOTHING while no application plays,
// and without filling that in, a recording that began in silence would have
// its sound pulled forward against the video. So every packet is laid at the
// position its QPC timestamp names and the holes are filled with silence.
#include "AudioCapture.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QHash>
#include <QUuid>

#include <windows.h>
#include <objbase.h>
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <sddl.h>
#include <tlhelp32.h>
#include <winver.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <future>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

// PKEY_Device_FriendlyName, spelled out so this file needs no INITGUID setup.
const PROPERTYKEY kFriendlyName = {
    {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

constexpr qint64 kTicksPerSecond = 10000000; // WASAPI time unit: 100 ns
// The engine-side buffer. The thread drains it every 10 ms, so 500 ms rides
// out a long scheduling stall without losing audio.
constexpr REFERENCE_TIME kEngineBuffer = kTicksPerSecond / 2;
constexpr DWORD kPipeBuffer = 64 * 1024;
// Application captures per recording: each is a thread, a pipe and an ffmpeg
// input (Linux caps its pw-record streams at the same number).
constexpr int kMaxApplicationCaptures = 8;

// From audioclientactivationparams.h, which MinGW-w64 does not ship: the
// activation blob of a process-loopback client.
const wchar_t kProcessLoopbackDevice[] = L"VAD\\Process_Loopback";
struct ProcessLoopbackParams {
    DWORD activationType = 1;  // AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK
    DWORD targetProcessId = 0;
    DWORD loopbackMode = 0;    // PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE
};

struct HandleCloser {
    void operator()(HANDLE h) const { CloseHandle(h); }
};
using UniqueHandle = std::unique_ptr<void, HandleCloser>;

// The PCM layout ffmpeg is told to read, and the WASAPI format producing it.
struct Format {
    WAVEFORMATEXTENSIBLE wave{};
    QString ffmpeg; // ffmpeg's raw demuxer for it

    int rate() const { return int(wave.Format.nSamplesPerSec); }
    int channels() const { return wave.Format.nChannels; }
    int bytesPerFrame() const { return wave.Format.nBlockAlign; }

    // 48 kHz stereo s16, the layout the Linux pulse input records.
    static Format standard()
    {
        Format f;
        f.wave.Format.wFormatTag = WAVE_FORMAT_PCM;
        f.wave.Format.nChannels = 2;
        f.wave.Format.nSamplesPerSec = 48000;
        f.wave.Format.wBitsPerSample = 16;
        f.wave.Format.nBlockAlign = 4;
        f.wave.Format.nAvgBytesPerSec = 48000 * 4;
        f.ffmpeg = QStringLiteral("s16le");
        return f;
    }

    // A device mix format; false when ffmpeg has no raw demuxer for it.
    bool assign(const WAVEFORMATEX &f)
    {
        const size_t size = sizeof(WAVEFORMATEX) + f.cbSize;
        if (size > sizeof(wave))
            return false;
        std::memcpy(&wave, &f, size);
        // The KSDATAFORMAT_SUBTYPE_* GUIDs of an extensible format carry the
        // plain format tag in Data1.
        const DWORD tag = f.wFormatTag == WAVE_FORMAT_EXTENSIBLE
                                  && f.cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)
                              ? wave.SubFormat.Data1
                              : f.wFormatTag;
        if (tag == WAVE_FORMAT_IEEE_FLOAT && f.wBitsPerSample == 32)
            ffmpeg = QStringLiteral("f32le");
        else if (tag == WAVE_FORMAT_PCM && f.wBitsPerSample == 16)
            ffmpeg = QStringLiteral("s16le");
        else if (tag == WAVE_FORMAT_PCM && f.wBitsPerSample == 24)
            ffmpeg = QStringLiteral("s24le");
        else if (tag == WAVE_FORMAT_PCM && f.wBitsPerSample == 32)
            ffmpeg = QStringLiteral("s32le");
        else
            return false;
        return true;
    }
};

// What the recorder and the capture thread share. The thread holds its own
// reference, so a thread left behind by a timed-out open never outlives it.
struct Shared {
    AudioCapture::Source source = AudioCapture::Source::System;
    std::wstring deviceId; // microphone endpoint id, empty = default input
    DWORD processId = 0;   // Application: the root of the captured tree
    // Application: held open so the id cannot pass to another process while
    // a lost stream is being reopened.
    HANDLE process = nullptr;
    HANDLE pipe = INVALID_HANDLE_VALUE;
    HANDLE stop = nullptr; // manual-reset: set once, the thread winds down
    Format format;         // written by the thread before it reports open

    ~Shared()
    {
        if (pipe != INVALID_HANDLE_VALUE)
            CloseHandle(pipe);
        if (stop)
            CloseHandle(stop);
        if (process)
            CloseHandle(process);
    }

    // Whether a lost stream is worth reopening: not for an application that
    // has exited.
    bool sourceAlive() const
    {
        return !process || WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    }
};

struct Stream {
    // The buffer event a process-loopback client insists on, though the pump
    // polls. First, so it is closed after the client lets go of it.
    UniqueHandle ready;
    ComPtr<IAudioClient> client;
    ComPtr<IAudioCaptureClient> capture;

    void reset()
    {
        capture.Reset();
        client.Reset();
        ready.reset();
    }
};

// Receives the completion of ActivateAudioInterfaceAsync, which Windows calls
// on a worker thread of its own (hence agile). Windows holds a reference until
// it has called, so a wait that timed out leaves nothing dangling.
class ActivationHandler final : public IActivateAudioInterfaceCompletionHandler,
                                public IAgileObject
{
public:
    ActivationHandler() : m_done(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    HANDLE done() const { return m_done; }

    STDMETHODIMP QueryInterface(REFIID riid, void **out) override
    {
        if (!out)
            return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IActivateAudioInterfaceCompletionHandler))
            *out = static_cast<IActivateAudioInterfaceCompletionHandler *>(this);
        else if (riid == __uuidof(IAgileObject))
            *out = static_cast<IAgileObject *>(this);
        else {
            *out = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++m_refs; }
    STDMETHODIMP_(ULONG) Release() override
    {
        const ULONG refs = --m_refs;
        if (refs == 0)
            delete this;
        return refs;
    }
    STDMETHODIMP ActivateCompleted(IActivateAudioInterfaceAsyncOperation *) override
    {
        SetEvent(m_done);
        return S_OK;
    }

private:
    ~ActivationHandler()
    {
        if (m_done)
            CloseHandle(m_done);
    }
    std::atomic<ULONG> m_refs{1};
    HANDLE m_done;
};

HRESULT activateProcessLoopback(DWORD processId, ComPtr<IAudioClient> &client)
{
    ProcessLoopbackParams params;
    params.targetProcessId = processId;
    PROPVARIANT activation;
    PropVariantInit(&activation);
    activation.vt = VT_BLOB;
    activation.blob.cbSize = sizeof(params);
    activation.blob.pBlobData = reinterpret_cast<BYTE *>(&params);
    ComPtr<ActivationHandler> handler;
    handler.Attach(new ActivationHandler);
    if (!handler->done())
        return HRESULT_FROM_WIN32(GetLastError());
    ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
    HRESULT hr = ActivateAudioInterfaceAsync(kProcessLoopbackDevice, __uuidof(IAudioClient),
                                             &activation, handler.Get(),
                                             operation.GetAddressOf());
    if (FAILED(hr))
        return hr;
    if (WaitForSingleObject(handler->done(), 2000) != WAIT_OBJECT_0)
        return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    HRESULT activated = E_FAIL;
    ComPtr<IUnknown> unknown;
    hr = operation->GetActivateResult(&activated, unknown.GetAddressOf());
    if (FAILED(hr))
        return hr;
    if (FAILED(activated))
        return activated;
    return unknown.As(&client);
}

// QueryPerformanceCounter in 100 ns ticks, the clock WASAPI stamps packets with.
qint64 qpcNow()
{
    static const qint64 freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f.QuadPart;
    }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    // Split so the multiply cannot overflow after days of uptime.
    return c.QuadPart / freq * kTicksPerSecond + c.QuadPart % freq * kTicksPerSecond / freq;
}

// Only this account may open the pipe: the default DACL of a named pipe lets
// Everyone read it, which would hand the microphone to every other user.
HANDLE createPipe(const std::wstring &name)
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return INVALID_HANDLE_VALUE;
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<BYTE> user(size);
    LPWSTR sid = nullptr;
    const bool haveSid = size > 0
                         && GetTokenInformation(token, TokenUser, user.data(), size, &size)
                         && ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(user.data())->User.Sid,
                                                   &sid);
    CloseHandle(token);
    if (!haveSid)
        return INVALID_HANDLE_VALUE;
    const std::wstring sddl = L"D:P(A;;GA;;;" + std::wstring(sid) + L")";
    LocalFree(sid);
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd,
                                                              nullptr))
        return INVALID_HANDLE_VALUE;
    SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
    const HANDLE pipe = CreateNamedPipeW(
        name.c_str(), PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, kPipeBuffer, 0, 0, &sa);
    LocalFree(sd);
    return pipe;
}

HRESULT openDevice(const Shared &s, ComPtr<IMMDevice> &device)
{
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void **>(enumerator.GetAddressOf()));
    if (FAILED(hr))
        return hr;
    if (s.source == AudioCapture::Source::System)
        return enumerator->GetDefaultAudioEndpoint(eRender, eConsole, device.GetAddressOf());
    if (s.deviceId.empty())
        return enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, device.GetAddressOf());
    hr = enumerator->GetDevice(s.deviceId.c_str(), device.GetAddressOf());
    if (FAILED(hr))
        return hr;
    // GetDevice also hands out an endpoint that is unplugged or disabled.
    DWORD state = 0;
    if (FAILED(device->GetState(&state)) || state != DEVICE_STATE_ACTIVE)
        return E_NOTFOUND;
    return S_OK;
}

// Process loopback: what one process tree plays, which the audio engine mixes
// into `format`. The client has no mix format to negotiate with.
HRESULT openApplicationStream(const Shared &s, const Format &format, Stream &st)
{
    HRESULT hr = activateProcessLoopback(s.processId, st.client);
    if (SUCCEEDED(hr))
        hr = st.client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                   AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK
                                       | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
                                       | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                   kEngineBuffer, 0, &format.wave.Format, nullptr);
    if (SUCCEEDED(hr)) {
        st.ready.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
        hr = st.ready ? st.client->SetEventHandle(st.ready.get())
                      : HRESULT_FROM_WIN32(GetLastError());
    }
    if (SUCCEEDED(hr))
        hr = st.client->GetService(__uuidof(IAudioCaptureClient),
                                   reinterpret_cast<void **>(st.capture.GetAddressOf()));
    if (FAILED(hr))
        st.reset();
    return hr;
}

// Opens the stream in `format`, the audio engine converting when the device
// mixes in something else. negotiate: if the engine will not convert, take
// the device's own mix format and report it back in `format`. A reopen after
// the device went away does not negotiate: ffmpeg was already told a format.
HRESULT openStream(const Shared &s, Format &format, bool negotiate, Stream &st)
{
    st.reset();
    if (s.source == AudioCapture::Source::Application)
        return openApplicationStream(s, format, st);
    ComPtr<IMMDevice> device;
    HRESULT hr = openDevice(s, device);
    if (FAILED(hr))
        return hr;
    const auto activate = [&] {
        st.client.Reset();
        return device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                reinterpret_cast<void **>(st.client.GetAddressOf()));
    };
    const DWORD loopback = s.source == AudioCapture::Source::System ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0;
    hr = activate();
    if (SUCCEEDED(hr))
        hr = st.client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                   loopback | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
                                       | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                   kEngineBuffer, 0, &format.wave.Format, nullptr);
    if (FAILED(hr) && hr != E_ACCESSDENIED && negotiate && SUCCEEDED(activate())) {
        WAVEFORMATEX *mix = nullptr;
        hr = st.client->GetMixFormat(&mix);
        if (SUCCEEDED(hr)) {
            Format native;
            hr = native.assign(*mix)
                     ? st.client->Initialize(AUDCLNT_SHAREMODE_SHARED, loopback, kEngineBuffer, 0,
                                             mix, nullptr)
                     : AUDCLNT_E_UNSUPPORTED_FORMAT;
            if (SUCCEEDED(hr))
                format = native;
            CoTaskMemFree(mix);
        }
    }
    if (SUCCEEDED(hr))
        hr = st.client->GetService(__uuidof(IAudioCaptureClient),
                                   reinterpret_cast<void **>(st.capture.GetAddressOf()));
    if (FAILED(hr))
        st.reset();
    return hr;
}

// Raw PCM on its way into the pipe: `pending` collects what was captured since
// the last write, `inflight` is the one overlapped write in progress. Bounded
// at two seconds of audio together; past that ffmpeg has stopped reading, and
// new audio is dropped instead of queued.
struct PipeWriter {
    HANDLE pipe = INVALID_HANDLE_VALUE;
    OVERLAPPED ov{};
    std::vector<char> pending;
    std::vector<char> inflight;
    size_t cap = 0;
    bool writing = false;
    bool broken = false; // ffmpeg closed its end
    bool warned = false;

    // Queues up to `frames` frames of `data` (silence when null) and returns
    // how many fit.
    qint64 append(const BYTE *data, qint64 frames, int bytesPerFrame)
    {
        const qint64 room = qint64(cap - pending.size() - inflight.size()) / bytesPerFrame;
        const qint64 n = qBound<qint64>(0, frames, room);
        if (n < frames && !warned) {
            warned = true;
            qWarning() << "AudioCapture: ffmpeg is not reading the audio pipe, dropping audio";
        }
        const size_t bytes = size_t(n) * size_t(bytesPerFrame);
        if (data)
            pending.insert(pending.end(), data, data + bytes);
        else
            pending.resize(pending.size() + bytes, 0);
        return n;
    }

    void flush()
    {
        if (writing) {
            DWORD done = 0;
            if (!GetOverlappedResult(pipe, &ov, &done, FALSE)) {
                if (GetLastError() != ERROR_IO_INCOMPLETE)
                    broken = true;
                return;
            }
            writing = false;
            inflight.clear();
        }
        if (pending.empty())
            return;
        inflight.swap(pending);
        if (WriteFile(pipe, inflight.data(), DWORD(inflight.size()), nullptr, &ov)) {
            inflight.clear();
            return;
        }
        if (GetLastError() == ERROR_IO_PENDING)
            writing = true;
        else
            broken = true;
    }

    void cancel()
    {
        if (!writing)
            return;
        CancelIoEx(pipe, &ov);
        DWORD done = 0;
        GetOverlappedResult(pipe, &ov, &done, TRUE);
        writing = false;
    }
};

// Waits for ffmpeg to open the pipe, or for stop.
bool connectPipe(const Shared &s, OVERLAPPED &ov)
{
    if (ConnectNamedPipe(s.pipe, &ov))
        return true;
    const DWORD error = GetLastError();
    if (error == ERROR_PIPE_CONNECTED)
        return true;
    if (error != ERROR_IO_PENDING)
        return false;
    const HANDLE waits[] = {s.stop, ov.hEvent};
    DWORD done = 0;
    if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) == WAIT_OBJECT_0 + 1)
        return GetOverlappedResult(s.pipe, &ov, &done, FALSE);
    CancelIoEx(s.pipe, &ov);
    GetOverlappedResult(s.pipe, &ov, &done, TRUE);
    return false;
}

void pump(const Shared &s, Stream &st)
{
    Format format = s.format;
    PipeWriter out;
    out.pipe = s.pipe;
    out.cap = size_t(format.rate()) * size_t(format.bytesPerFrame()) * 2;
    out.ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!out.ov.hEvent)
        return;
    if (!connectPipe(s, out.ov)) {
        CloseHandle(out.ov.hEvent);
        return;
    }

    const qint64 rate = format.rate();
    const int bytesPerFrame = format.bytesPerFrame();
    // A packet within 20 ms of where the count says is jitter, not a hole.
    // Past it the count is corrected, which also absorbs the drift between
    // the device clock and QPC (tens of ppm: one correction every few minutes).
    const qint64 tolerance = rate / 50;
    // Silence is laid this far behind the clock, so a packet still on its way
    // finds its place free instead of overlapping the filler.
    const qint64 lag = rate / 20;
    if (FAILED(st.client->Start()))
        st.reset();
    const qint64 t0 = qpcNow();
    qint64 written = 0; // frames handed to the pipe since t0
    qint64 retryAt = 0;
    const auto frameAt = [&](qint64 ticks) { return (ticks - t0) * rate / kTicksPerSecond; };
    const auto fillTo = [&](qint64 frame) {
        if (frame > written)
            written += out.append(nullptr, frame - written, bytesPerFrame);
    };

    while (WaitForSingleObject(s.stop, 10) == WAIT_TIMEOUT && !out.broken) {
        bool gotPacket = false;
        if (st.capture) {
            HRESULT hr = S_OK;
            for (;;) {
                UINT32 packet = 0;
                hr = st.capture->GetNextPacketSize(&packet);
                if (FAILED(hr) || packet == 0)
                    break;
                BYTE *data = nullptr;
                UINT32 frames = 0;
                DWORD flags = 0;
                UINT64 qpc = 0;
                hr = st.capture->GetBuffer(&data, &frames, &flags, nullptr, &qpc);
                if (FAILED(hr) || hr == AUDCLNT_S_BUFFER_EMPTY)
                    break;
                gotPacket = true;
                qint64 skip = 0;
                // A zero position is no timestamp at all; laid at it, the
                // packet would land days before t0 and be skipped whole.
                if (!(flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) && qpc != 0) {
                    const qint64 at = frameAt(qint64(qpc));
                    if (at > written + tolerance)
                        fillTo(at);
                    else if (at < written - tolerance)
                        skip = qMin<qint64>(written - at, frames);
                }
                if (frames > skip) {
                    const BYTE *from = (flags & AUDCLNT_BUFFERFLAGS_SILENT)
                                           ? nullptr
                                           : data + skip * bytesPerFrame;
                    written += out.append(from, frames - skip, bytesPerFrame);
                }
                st.capture->ReleaseBuffer(frames);
            }
            if (FAILED(hr)) {
                // AUDCLNT_E_DEVICE_INVALIDATED: unplugged or disabled. The
                // recording goes on in silence and the device is retried.
                qWarning() << "AudioCapture: audio device lost, recording silence until it returns, hr"
                           << Qt::hex << ulong(hr);
                st.reset();
                retryAt = qpcNow() + kTicksPerSecond;
            }
        } else if (qpcNow() >= retryAt && s.sourceAlive()) {
            if (FAILED(openStream(s, format, false, st)) || FAILED(st.client->Start())) {
                st.reset();
                retryAt = qpcNow() + kTicksPerSecond;
            }
        }
        if (!gotPacket)
            fillTo(frameAt(qpcNow()) - lag);
        out.flush();
    }
    if (st.client)
        st.client->Stop();
    out.cancel();
    CloseHandle(out.ov.hEvent);
}

void run(std::shared_ptr<Shared> s, std::promise<HRESULT> opened)
{
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    {
        Stream st;
        Format format = s->format;
        const HRESULT hr = FAILED(co) ? co : openStream(*s, format, true, st);
        s->format = format;
        opened.set_value(hr);
        if (SUCCEEDED(hr))
            pump(*s, st);
    }
    if (SUCCEEDED(co))
        CoUninitialize();
}

QString openError(AudioCapture::Source source, bool defaultInput, HRESULT hr)
{
    if (hr == E_ACCESSDENIED && source == AudioCapture::Source::Microphone)
        return QCoreApplication::translate(
            "AudioCapture",
            "Windows does not let desktop apps use the microphone. Allow it in Settings > Privacy & security > Microphone.");
    if ((hr == E_NOTFOUND || hr == AUDCLNT_E_DEVICE_INVALIDATED)
        && source != AudioCapture::Source::Application) {
        if (source == AudioCapture::Source::System)
            return QCoreApplication::translate("AudioCapture",
                                               "There is no playback device to record system audio from");
        return defaultInput
                   ? QCoreApplication::translate("AudioCapture", "No microphone is connected")
                   : QCoreApplication::translate("AudioCapture",
                                                 "The selected microphone is not connected");
    }
    return QCoreApplication::translate("AudioCapture", "Could not open the audio device (error 0x%1)")
        .arg(ulong(hr), 8, 16, QLatin1Char('0'));
}

// Process loopback arrived in Windows 10 version 2004, build 19041.
// RtlGetVersion, because GetVersionEx reports 8.1 to an exe whose manifest
// does not list Windows 10.
bool processLoopbackSupported()
{
    static const bool supported = [] {
        using GetVersion = LONG(WINAPI *)(OSVERSIONINFOW *);
        const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        const auto get = ntdll ? reinterpret_cast<GetVersion>(
                                     reinterpret_cast<void *>(GetProcAddress(ntdll, "RtlGetVersion")))
                               : nullptr;
        OSVERSIONINFOW info{};
        info.dwOSVersionInfoSize = sizeof(info);
        return get && get(&info) == 0
               && (info.dwMajorVersion > 10
                   || (info.dwMajorVersion == 10 && info.dwBuildNumber >= 19041));
    }();
    return supported;
}

// The id an application is stored under: its executable's file name without
// ".exe", lowercased, so a session and a process list agree on it.
QString appId(const QString &exe)
{
    QString id = QFileInfo(exe).fileName().toLower();
    if (id.endsWith(QLatin1String(".exe")))
        id.chop(4);
    return id;
}

// Full path of a running process's executable; empty once it has exited.
QString processImage(DWORD processId)
{
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process)
        return {};
    wchar_t path[MAX_PATH * 4];
    DWORD size = DWORD(std::size(path));
    const bool ok = QueryFullProcessImageNameW(process, 0, path, &size);
    CloseHandle(process);
    return ok ? QString::fromWCharArray(path, int(size)) : QString();
}

// What the user calls an application: its version resource's FileDescription
// ("Google Chrome" for chrome.exe), else the file name.
QString appLabel(const QString &image)
{
    const std::wstring file = image.toStdWString();
    DWORD unused = 0;
    const DWORD size = GetFileVersionInfoSizeW(file.c_str(), &unused);
    std::vector<BYTE> data(size);
    struct Translation {
        WORD language;
        WORD codePage;
    };
    Translation *translation = nullptr;
    wchar_t *text = nullptr;
    UINT length = 0;
    if (size > 0 && GetFileVersionInfoW(file.c_str(), 0, size, data.data())
        && VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation",
                          reinterpret_cast<void **>(&translation), &length)
        && length >= sizeof(Translation)) {
        const std::wstring key = QStringLiteral("\\StringFileInfo\\%1%2\\FileDescription")
                                     .arg(translation->language, 4, 16, QLatin1Char('0'))
                                     .arg(translation->codePage, 4, 16, QLatin1Char('0'))
                                     .toStdWString();
        if (VerQueryValueW(data.data(), key.c_str(), reinterpret_cast<void **>(&text), &length)
            && length > 1) {
            const QString label = QString::fromWCharArray(text).trimmed();
            if (!label.isEmpty())
                return label;
        }
    }
    return QFileInfo(image).completeBaseName();
}

} // namespace

struct AudioCapture::Private {
    std::shared_ptr<Shared> s;
    std::thread thread;
    QString url;

    ~Private()
    {
        // The thread notices within one 10 ms poll; a write still pending is
        // cancelled, so the join is bounded.
        SetEvent(s->stop);
        thread.join();
    }
};

AudioCapture::AudioCapture(std::unique_ptr<Private> p) : d(std::move(p)) {}
AudioCapture::~AudioCapture() = default;

std::unique_ptr<AudioCapture> AudioCapture::open(Source source, const QString &device,
                                                 QString *error)
{
    auto s = std::make_shared<Shared>();
    s->source = source;
    if (source == Source::Microphone)
        s->deviceId = device.trimmed().toStdWString();
    if (source == Source::Application) {
        s->processId = device.toULong();
        s->process = s->processId ? OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                                                FALSE, s->processId)
                                  : nullptr;
        if (!s->process) {
            const HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
            if (error)
                *error = openError(source, false, hr);
            return nullptr;
        }
    }
    s->format = Format::standard();
    const QString url = QStringLiteral("\\\\.\\pipe\\unisic-audio-%1-%2")
                            .arg(GetCurrentProcessId())
                            .arg(QUuid::createUuid().toString(QUuid::Id128));
    s->stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (s->stop)
        s->pipe = createPipe(url.toStdWString());
    if (s->pipe == INVALID_HANDLE_VALUE) {
        qWarning() << "AudioCapture: could not create the audio pipe, error" << GetLastError();
        if (error)
            *error = QCoreApplication::translate("AudioCapture", "Could not set up audio recording");
        return nullptr;
    }

    std::promise<HRESULT> opened;
    std::future<HRESULT> result = opened.get_future();
    std::thread thread(run, s, std::move(opened));
    // Opening an endpoint is a local call into the audio service that takes a
    // few milliseconds; a service that does not answer must not freeze the GUI.
    if (result.wait_for(std::chrono::seconds(3)) != std::future_status::ready) {
        SetEvent(s->stop);
        thread.detach(); // holds its own share of `s` and exits once the call returns
        qWarning() << "AudioCapture: the Windows audio service did not answer";
        if (error)
            *error = QCoreApplication::translate("AudioCapture", "The Windows audio service is not responding");
        return nullptr;
    }
    const HRESULT hr = result.get();
    if (FAILED(hr)) {
        thread.join(); // it returns right after reporting a failed open
        qWarning() << "AudioCapture: opening the audio device failed, hr" << Qt::hex << ulong(hr);
        if (error)
            *error = openError(source, s->deviceId.empty(), hr);
        return nullptr;
    }
    auto p = std::make_unique<Private>();
    p->s = std::move(s);
    p->thread = std::move(thread);
    p->url = url;
    return std::unique_ptr<AudioCapture>(new AudioCapture(std::move(p)));
}

QStringList AudioCapture::ffmpegInput() const
{
    const Format &f = d->s->format;
    return {QStringLiteral("-thread_queue_size"), QStringLiteral("1024"),
            QStringLiteral("-f"), f.ffmpeg,
            QStringLiteral("-ar"), QString::number(f.rate()),
            QStringLiteral("-ac"), QString::number(f.channels()),
            QStringLiteral("-i"), d->url};
}

bool AudioCapture::inputListAvailable()
{
    return true;
}

QVariantList AudioCapture::inputDevices()
{
    // The GUI thread is already a single-threaded apartment (Qt's OLE setup):
    // CoInitializeEx then reports RPC_E_CHANGED_MODE and COM still works.
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    QVariantList out;
    {
        ComPtr<IMMDeviceEnumerator> enumerator;
        ComPtr<IMMDeviceCollection> devices;
        UINT count = 0;
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                       __uuidof(IMMDeviceEnumerator),
                                       reinterpret_cast<void **>(enumerator.GetAddressOf())))
            && SUCCEEDED(enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE,
                                                        devices.GetAddressOf()))
            && SUCCEEDED(devices->GetCount(&count))) {
            for (UINT i = 0; i < count; ++i) {
                ComPtr<IMMDevice> device;
                LPWSTR rawId = nullptr;
                if (FAILED(devices->Item(i, device.GetAddressOf())) || FAILED(device->GetId(&rawId)))
                    continue;
                const QString id = QString::fromWCharArray(rawId);
                CoTaskMemFree(rawId);
                QString label;
                ComPtr<IPropertyStore> props;
                if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, props.GetAddressOf()))) {
                    PROPVARIANT name;
                    PropVariantInit(&name);
                    if (SUCCEEDED(props->GetValue(kFriendlyName, &name)) && name.vt == VT_LPWSTR)
                        label = QString::fromWCharArray(name.pwszVal);
                    PropVariantClear(&name);
                }
                out.append(QVariantMap{{QStringLiteral("id"), id},
                                       {QStringLiteral("label"), label.isEmpty() ? id : label}});
            }
        }
    }
    if (SUCCEEDED(co))
        CoUninitialize();
    return out;
}

bool AudioCapture::applicationListAvailable()
{
    return processLoopbackSupported();
}

QVariantList AudioCapture::applications()
{
    if (!processLoopbackSupported())
        return {};
    // Every audio session of every playback device. A session lives from an
    // application's first sound until it exits, so a quiet one is listed with
    // streams 0: it can be ticked ahead and is recorded once it plays.
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    QVariantList out;
    {
        const DWORD self = GetCurrentProcessId();
        QHash<QString, qsizetype> at; // id -> index in out
        ComPtr<IMMDeviceEnumerator> enumerator;
        ComPtr<IMMDeviceCollection> devices;
        UINT count = 0;
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                       __uuidof(IMMDeviceEnumerator),
                                       reinterpret_cast<void **>(enumerator.GetAddressOf())))
            && SUCCEEDED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE,
                                                        devices.GetAddressOf()))
            && SUCCEEDED(devices->GetCount(&count))) {
            for (UINT i = 0; i < count; ++i) {
                ComPtr<IMMDevice> device;
                ComPtr<IAudioSessionManager2> manager;
                ComPtr<IAudioSessionEnumerator> sessions;
                int sessionCount = 0;
                if (FAILED(devices->Item(i, device.GetAddressOf()))
                    || FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                                               reinterpret_cast<void **>(manager.GetAddressOf())))
                    || FAILED(manager->GetSessionEnumerator(sessions.GetAddressOf()))
                    || FAILED(sessions->GetCount(&sessionCount)))
                    continue;
                for (int j = 0; j < sessionCount; ++j) {
                    ComPtr<IAudioSessionControl> control;
                    ComPtr<IAudioSessionControl2> session;
                    DWORD processId = 0;
                    AudioSessionState state = AudioSessionStateExpired;
                    if (FAILED(sessions->GetSession(j, control.GetAddressOf()))
                        || FAILED(control.As(&session))
                        || session->IsSystemSoundsSession() == S_OK
                        || FAILED(session->GetProcessId(&processId)) || processId == 0
                        || processId == self || FAILED(session->GetState(&state))
                        || state == AudioSessionStateExpired)
                        continue;
                    const QString image = processImage(processId);
                    if (image.isEmpty())
                        continue;
                    const QString id = appId(image);
                    const int playing = state == AudioSessionStateActive ? 1 : 0;
                    if (const auto it = at.constFind(id); it != at.constEnd()) {
                        QVariantMap m = out[*it].toMap();
                        m[QStringLiteral("streams")] = m.value(QStringLiteral("streams")).toInt() + playing;
                        out[*it] = m;
                        continue;
                    }
                    at.insert(id, out.size());
                    out.append(QVariantMap{{QStringLiteral("id"), id},
                                           {QStringLiteral("label"), appLabel(image)},
                                           {QStringLiteral("icon"), QString()},
                                           {QStringLiteral("streams"), playing}});
                }
            }
        }
    }
    if (SUCCEEDED(co))
        CoUninitialize();
    return out;
}

QList<AudioCapture::AppTarget> AudioCapture::applicationTargets(const QStringList &apps)
{
    if (apps.isEmpty() || !processLoopbackSupported())
        return {};
    struct Process {
        DWORD parent;
        QString id;
    };
    QHash<DWORD, Process> processes;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return {};
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry))
        processes.insert(entry.th32ProcessID,
                         {entry.th32ParentProcessID, appId(QString::fromWCharArray(entry.szExeFile))});
    CloseHandle(snapshot);

    // One capture per process tree, at its topmost process of the same
    // executable: the capture takes in every child, a child started
    // mid-recording too, so a browser whose audio runs in a helper process is
    // one capture. Ordered as ticked, then by process id, so the tracks of
    // two recordings line up.
    const DWORD self = GetCurrentProcessId();
    QList<AppTarget> targets;
    for (const QString &app : apps) {
        QList<DWORD> roots;
        for (auto it = processes.cbegin(); it != processes.cend(); ++it) {
            if (it->id != app || it.key() == self)
                continue;
            DWORD root = it.key();
            // Bounded: a parent id the snapshot holds can be a reused one.
            for (int depth = 0; depth < 64; ++depth) {
                const auto parent = processes.constFind(processes.value(root).parent);
                if (parent == processes.cend() || parent.key() == root || parent->id != app)
                    break;
                root = parent.key();
            }
            if (!roots.contains(root))
                roots.append(root);
        }
        std::sort(roots.begin(), roots.end());
        for (const DWORD root : std::as_const(roots)) {
            if (targets.size() == kMaxApplicationCaptures) {
                qWarning() << "AudioCapture: application audio capped at" << kMaxApplicationCaptures
                           << "process trees";
                return targets;
            }
            const QString image = processImage(root);
            targets.append({QString::number(root), image.isEmpty() ? app : appLabel(image)});
        }
    }
    return targets;
}

QString AudioCapture::backend()
{
    return QStringLiteral("wasapi");
}
