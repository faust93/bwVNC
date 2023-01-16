#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

//#define MA_DEBUG_OUTPUT
#define MA_NO_WEBAUDIO
#define MA_NO_NULL
#define MA_NO_ENCODING
#define MA_NO_NODE_GRAPH
#define MA_NO_GENERATION

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

/*
50ms callback = 1102 frames and 4408 bytes buffer size per callback iteration
frames count * byter per frame (4) = 4408 bytes
*/
#define CALLBACK_PERIOD_SZ 20 /* msec */

size_t maxNetworkJitterInMillisec = 1000;

uint8_t sampleFormatU8  = 0;
uint8_t sampleFormatS8  = 1;
uint8_t sampleFormatU16 = 2;
uint8_t sampleFormatS16 = 3;
uint8_t sampleFormatU32 = 4;
uint8_t sampleFormatS32 = 5;

uint8_t bitsPerSample[6] = {8, 8, 16, 16, 32, 32};

ma_device      aDev;
uint8_t        sampleFormat, numberOfChannels;
uint32_t       samplingFreq;
uint8_t*       bufPtr;

size_t         bufTotalSize;
size_t         bufFreeSize;
size_t         bufUnsubmittedSize;
size_t         bufSubmittedHead;
size_t         bufUnsubmittedHead;
uint32_t       extraDelayInMillisec;

bool           audioEngineState,cbLock = false;

/* playback buffer size */
ma_uint32 fbytes;

#define GetSampleSize (numberOfChannels << (sampleFormat >> 1))

void audioOutCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
  if(!bufUnsubmittedSize)
    return;

  cbLock = true;
  fbytes = frameCount * ma_get_bytes_per_frame(pDevice->playback.format, pDevice->playback.channels);

  size_t io_bytes = bufUnsubmittedSize;
  if(io_bytes > fbytes)
     io_bytes = fbytes;

  size_t pOutOffset = 0;
  size_t bytes_left_to_copy = io_bytes;
  while (bytes_left_to_copy != 0) {
      size_t bytes_to_copy = bytes_left_to_copy;

      if (bytes_to_copy + bufSubmittedHead > bufTotalSize)
        bytes_to_copy = bufTotalSize - bufSubmittedHead;
      if (bytes_to_copy == 0)
        goto cb_exit;

      MA_COPY_MEMORY(pOutput + pOutOffset, bufPtr + bufSubmittedHead, bytes_to_copy);
      bufSubmittedHead = ((bufSubmittedHead + bytes_to_copy) & (bufTotalSize - 1));
      pOutOffset += bytes_to_copy;
      bytes_left_to_copy -= bytes_to_copy;
      bufUnsubmittedSize -= bytes_to_copy;
      bufFreeSize += bytes_to_copy;
  }

cb_exit:
  cbLock = false;
}

static rfbBool audioInit(uint8_t fmt, uint8_t channels, uint32_t frequency)
{
  uint8_t  bits_per_sample = bitsPerSample[fmt];

  sampleFormat     = ((bits_per_sample == 8) ? sampleFormatU8 : sampleFormatS16);
  numberOfChannels = channels;
  samplingFreq     = frequency;

  if (audioEngineState)
     return false;

  // allocate buffer
  size_t buf_estim_size = (4 * maxNetworkJitterInMillisec * samplingFreq) / 1000;

  size_t buf_alloc_size = 1;
  while (buf_alloc_size < buf_estim_size)
    buf_alloc_size <<= 1;

  size_t sample_size = GetSampleSize;

  bufPtr = ((uint8_t*)( calloc(buf_alloc_size, sample_size) ));
  if (bufPtr == NULL)
     return false;

  bufTotalSize = bufFreeSize = buf_alloc_size * sample_size;
  bufUnsubmittedSize = bufSubmittedHead = bufUnsubmittedHead = 0;

  ma_device_config aDevConfig;
  aDevConfig                    = ma_device_config_init(ma_device_type_playback);
  aDevConfig.playback.format    = ma_format_s16;
  aDevConfig.playback.channels  = numberOfChannels;
  aDevConfig.sampleRate         = samplingFreq;
  aDevConfig.dataCallback       = audioOutCallback;
  aDevConfig.performanceProfile = ma_performance_profile_low_latency;
  //aDevConfig.noFixedSizedCallback = true;
  aDevConfig.periodSizeInMilliseconds = CALLBACK_PERIOD_SZ;;
//  aDevConfig.pUserData         = this;

  if (ma_device_init(NULL, &aDevConfig, &aDev) != MA_SUCCESS)
     return false;

  if (ma_device_start(&aDev) != MA_SUCCESS)
     return false;

  audioEngineState = true;

  return true;
}

void addSilentSamples(size_t numberOfSamples)
{
  if (audioEngineState) {
    size_t bytes_left_to_add = numberOfSamples * GetSampleSize;
    while (bytes_left_to_add != 0) {
      size_t bytes_to_add = bytes_left_to_add;
      if (bytes_to_add > bufFreeSize)
        bytes_to_add = bufFreeSize;
      if (bytes_to_add + bufUnsubmittedHead > bufTotalSize)
        bytes_to_add = bufTotalSize - bufUnsubmittedHead;
      if (bytes_to_add == 0)
        break;

      memset(bufPtr + bufUnsubmittedHead, ((sampleFormat == sampleFormatU8) ? 0x80 : 0), bytes_to_add);
      bufUnsubmittedHead  = ((bufUnsubmittedHead + bytes_to_add) & (bufTotalSize - 1));
      bufFreeSize        -= bytes_to_add;
      bufUnsubmittedSize += bytes_to_add;
      bytes_left_to_add  -= bytes_to_add;
    }
  }
}

size_t addSamples(rfbClient* client, uint8_t* data, size_t size)
{
  while(cbLock)
    usleep(5);

  /* skip sample in case of delay > CALLBACK_PERIOD_SZ * 5 */
  if (bufUnsubmittedSize > fbytes * 5) {
    size = size / 2;
    rfbClientLog("%s: audio frame skiped, chunk size trimmed to %d bufUnsubmittedSize=%d framebytes=%d\n",__FUNCTION__, size, bufUnsubmittedSize, fbytes);
  }
  
  if (audioEngineState && size > 0) {
    size_t bytes_left_to_copy = size;
    while (bytes_left_to_copy != 0) {
      size_t bytes_to_copy = bytes_left_to_copy;

      if (bytes_to_copy > bufFreeSize)
        bytes_to_copy = bufFreeSize;
      if (bytes_to_copy + bufUnsubmittedHead > bufTotalSize)
        bytes_to_copy = bufTotalSize - bufUnsubmittedHead;
      if (bytes_to_copy == 0)
        break;

      memcpy(bufPtr + bufUnsubmittedHead, data, bytes_to_copy);
      bufUnsubmittedHead  = ((bufUnsubmittedHead + bytes_to_copy) & (bufTotalSize - 1));
      bufFreeSize        -= bytes_to_copy;
      bufUnsubmittedSize += bytes_to_copy;
      data               += bytes_to_copy;
      bytes_left_to_copy -= bytes_to_copy;
    }
    client->clientStats.audioBytesRx += size;
    client->clientStats.audioPendingBytes = bufUnsubmittedSize;
    client->clientStats.audioPendingMs = ((bufUnsubmittedSize / GetSampleSize) / CALLBACK_PERIOD_SZ);
  }
  return size;
}

void notifyStreamingStartStop(uint8_t isStart)
{
  if (isStart) {
    // suppress audio stuttering caused by network jitter:
    // add 20+ milliseconds of silence (playback delay) ahead of actual samples
    size_t delay_in_millisec = 20 + extraDelayInMillisec;
    addSilentSamples(delay_in_millisec * samplingFreq / 1000);
  }
}

void audioStop(void)
{
  if (audioEngineState) {
    audioEngineState = false;
    ma_device_uninit(&aDev);
    free(bufPtr);
  }
}
