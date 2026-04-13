#ifndef RAGE_SOUND_WASAPI_H
#define RAGE_SOUND_WASAPI_H

#include <Audioclient.h>
#include <Mmdeviceapi.h>

#include <cstdint>

#include "RageSound.h"
#include "RageSoundDriver.h"
#include "RageThreads.h"

class RageSoundDriver_WASAPI : public RageSoundDriver {
 public:
  RageSoundDriver_WASAPI();
  virtual ~RageSoundDriver_WASAPI();

  std::string Init();

  int64_t GetPosition() const;
  inline int GetSampleRate() const { return m_SampleRate; }

 protected:
  void AudioThread();

  int64_t m_LastPosition;

  // WASAPI
  IMMDevice* m_Device;
  IAudioClient* m_AudioClient;
  IAudioRenderClient* m_RenderClient;
  IAudioClock* m_AudioClock;

  HANDLE m_Event;
  HANDLE m_Thread;

  UINT32 m_BufferFrames;
  int m_SampleRate;

  bool m_Running;
};

#endif