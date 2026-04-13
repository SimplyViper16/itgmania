#include "RageSoundDriver_WASAPI.h"

#include <mmreg.h>
#include <windows.h>

#include <stdexcept>

#include "RageLog.h"

REGISTER_SOUND_DRIVER_CLASS2(WASAPI, WASAPI);

RageSoundDriver_WASAPI::RageSoundDriver_WASAPI()
    : m_LastPosition(0),
      m_Device(nullptr),
      m_AudioClient(nullptr),
      m_RenderClient(nullptr),
      m_AudioClock(nullptr),
      m_Event(nullptr),
      m_Thread(nullptr),
      m_BufferFrames(0),
      m_SampleRate(48000),
      m_Running(false) {}

RageSoundDriver_WASAPI::~RageSoundDriver_WASAPI() {
  m_Running = false;

  if (m_Thread) {
    WaitForSingleObject(m_Thread, INFINITE);
  }

  if (m_AudioClient) {
    m_AudioClient->Stop();
    m_AudioClient = nullptr;
  }

  if (m_Event) {
    CloseHandle(m_Event);
    m_Event = nullptr;
  }

  if (m_AudioClock) {
    m_AudioClock->Release();
    m_AudioClock = nullptr;
  }

  if (m_RenderClient) {
    m_RenderClient->Release();
    m_RenderClient = nullptr;
  }

  if (m_AudioClient) {
    m_AudioClient->Release();
    m_AudioClient = nullptr;
  }

  if (m_Device) {
    m_Device->Release();
    m_Device = nullptr;
  }
}

std::string RageSoundDriver_WASAPI::Init() {
  HRESULT hr;

  IMMDeviceEnumerator* enumerator = nullptr;
  hr = CoCreateInstance(
      __uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
      __uuidof(IMMDeviceEnumerator), (void**)&enumerator);
  if (FAILED(hr)) {
    return "Failed to create device enumerator";
  }

  hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &m_Device);
  if (enumerator) {
    enumerator->Release();
    enumerator = nullptr;
  }

  if (FAILED(hr)) {
    return "Failed to get default audio device";
  }

  hr = m_Device->Activate(
      __uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&m_AudioClient);
  if (FAILED(hr)) {
    return "Failed to activate audio client";
  }

  WAVEFORMATEX fmt = {};
  fmt.wFormatTag = WAVE_FORMAT_PCM;
  fmt.nChannels = 2;
  fmt.nSamplesPerSec = m_SampleRate;
  fmt.wBitsPerSample = 16;
  fmt.nBlockAlign = fmt.nChannels * (fmt.wBitsPerSample / 8);
  fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
  fmt.cbSize = 0;

  REFERENCE_TIME bufferDuration = 200000;  // 20ms

  m_Event = CreateEvent(NULL, FALSE, FALSE, NULL);

  // use SHARED for streaming
  hr = m_AudioClient->Initialize(
      AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
      bufferDuration, 0, &fmt, NULL);

  if (FAILED(hr)) {
    return "AudioClient Initialize failed";
  }

  hr = m_AudioClient->SetEventHandle(m_Event);
  if (FAILED(hr)) {
    return "SetEventHandle failed";
  }

  hr = m_AudioClient->GetBufferSize(&m_BufferFrames);
  if (FAILED(hr)) {
    return "GetBufferSize failed";
  }

  hr = m_AudioClient->GetService(
      __uuidof(IAudioRenderClient), (void**)&m_RenderClient);
  if (FAILED(hr)) {
    return "Get RenderClient failed";
  }

  hr = m_AudioClient->GetService(__uuidof(IAudioClock), (void**)&m_AudioClock);
  if (FAILED(hr)) {
    return "Get AudioClock failed";
  }

  StartDecodeThread();

  hr = m_AudioClient->Start();
  if (FAILED(hr)) {
    return "AudioClient Start failed";
  }

  m_Running = true;

  m_Thread = CreateThread(
      NULL, 0,
      [](LPVOID param) -> DWORD {
        ((RageSoundDriver_WASAPI*)param)->AudioThread();
        return 0;
      },
      this, 0, NULL);

  return "";
}

int64_t RageSoundDriver_WASAPI::GetPosition() const {
  if (!m_AudioClock) {
    return 0;
  }

  UINT64 pos = 0;
  UINT64 qpc = 0;
  UINT64 freq = 0;

  if (FAILED(m_AudioClock->GetPosition(&pos, &qpc))) {
    return 0;
  }

  if (FAILED(m_AudioClock->GetFrequency(&freq)) || freq == 0) {
    return 0;
  }

  // manage as int
  return (int64_t)((pos * m_SampleRate) / freq);
}

void RageSoundDriver_WASAPI::AudioThread() {
  while (m_Running) {
    WaitForSingleObject(m_Event, INFINITE);

    UINT32 padding = 0;
    m_AudioClient->GetCurrentPadding(&padding);

    UINT32 framesToWrite = m_BufferFrames - padding;
    if (framesToWrite == 0) {
      continue;
    }

    BYTE* pData = nullptr;
    m_RenderClient->GetBuffer(framesToWrite, &pData);

    int64_t curPos = GetPosition();

    int64_t pos1 = m_LastPosition;
    int64_t pos2 = pos1 + framesToWrite;

    Mix((int16_t*)pData, framesToWrite, pos1, curPos);

    m_RenderClient->ReleaseBuffer(framesToWrite, 0);

    m_LastPosition = pos2;
  }
}
