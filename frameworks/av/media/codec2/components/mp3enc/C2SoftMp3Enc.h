/*
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

#ifndef ANDROID_C2_SOFT_MP3_ENC_H_
#define ANDROID_C2_SOFT_MP3_ENC_H_

#include <atomic>
#include <optional>
#include <SimpleC2Component.h>

#include "lame.h"

namespace android {

class C2SoftMp3Enc : public SimpleC2Component {
public:
    class IntfImpl;

    C2SoftMp3Enc(const char* name,
                 c2_node_id_t id,
                 const std::shared_ptr<IntfImpl>& intfImpl);
    virtual ~C2SoftMp3Enc();

    // From SimpleC2Component
    c2_status_t onInit() override;
    c2_status_t onStop() override;
    void onReset() override;
    void onRelease() override;
    c2_status_t onFlush_sm() override;

    void process(const std::unique_ptr<C2Work>& work,
                 const std::shared_ptr<C2BlockPool>& pool) override;

    c2_status_t drain(uint32_t drainMode,
                      const std::shared_ptr<C2BlockPool>& pool) override;

private:
    std::shared_ptr<IntfImpl> mIntf;

    // LAME encoder handle
    lame_global_flags* mLameEncoder;

    // Input frame sizing
    uint32_t mNumBytesPerInputFrame; // bytes of PCM16 per full MP3 frame
    uint32_t mOutBufferSize;         // max bytes per encoded frame

    // State tracking
    bool mSentCodecSpecificData;     // MP3 doesn't need CSD, but keep for framework consistency
    size_t mInputSize;
    std::optional<c2_cntr64_t> mNextFrameTimestampUs;
    std::optional<c2_cntr64_t> mLastFrameEndTimestampUs;

    bool mSignalledError;
    std::atomic_uint64_t mOutIndex;

    // Remainder buffer for partial frames between calls
    // Buffer size: max channels (2) * max samples per frame (1152) * sizeof(int16_t)
    uint8_t mRemainder[6 * 1152 * sizeof(int16_t)];//to avoid buffer overflow ,keep the size more
    size_t mRemainderLen;

    // MP3-specific parameters
    uint32_t mSamplesPerFrame;       // 1152 for MPEG1, 576 for MPEG2
    uint32_t mNumChannels;           // Cached channel count
    uint32_t mSampleRate;            // Cached sample rate
    uint32_t mBitrate;               // Cached bitrate

    // Internal helper functions
    status_t initEncoder();
    status_t setAudioParams();
    
    // LAME-specific helper functions
    int encodeMonoFrame(const int16_t* pcm, uint8_t* mp3Buffer, size_t bufferSize);
    int encodeStereoFrame(const int16_t* pcm, uint8_t* mp3Buffer, size_t bufferSize);
    int flushEncoder(uint8_t* mp3Buffer, size_t bufferSize);
    
    // Buffer management
    void fillPcmBuffer(int16_t* pcmBuffer, size_t& samplesFilled);
    void updateRemainder(const uint8_t* newData, size_t dataSize);

    C2_DO_NOT_COPY(C2SoftMp3Enc);
};

} // namespace android

#endif // ANDROID_C2_SOFT_MP3_ENC_H_
