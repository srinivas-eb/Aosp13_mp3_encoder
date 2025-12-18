

#define LOG_NDEBUG 0
#define LOG_TAG "C2SoftMp3Enc"
#include <utils/Log.h>

#include <inttypes.h>
#include <string.h>
#include <algorithm>

#include <C2PlatformSupport.h>
#include <SimpleC2Interface.h>
#include <media/stagefright/foundation/MediaDefs.h>
#include <media/stagefright/foundation/hexdump.h>
//#include <C2BufferUtils.h>
#include <C2Debug.h>

#include "C2SoftMp3Enc.h"

namespace android {

namespace {

constexpr char COMPONENT_NAME[] = "c2.android.mp3.encoder";

}  // namespace

class C2SoftMp3Enc::IntfImpl : public SimpleInterface<void>::BaseParams {
public:
    explicit IntfImpl(const std::shared_ptr<C2ReflectorHelper> &helper)
        : SimpleInterface<void>::BaseParams(
                helper,
                COMPONENT_NAME,
                C2Component::KIND_ENCODER,
                C2Component::DOMAIN_AUDIO,
                MEDIA_MIMETYPE_AUDIO_MPEG) {
        ALOGI("Entered in to the IntfImpl");
        noPrivateBuffers();
        noInputReferences();
        noOutputReferences();
        noInputLatency();
        noTimeStretch();
        setDerivedInstance(this);

        addParameter(
                DefineParam(mAttrib, C2_PARAMKEY_COMPONENT_ATTRIBUTES)
                .withConstValue(new C2ComponentAttributesSetting(
                    C2Component::ATTRIB_IS_TEMPORAL))
                .build());

        addParameter(
                DefineParam(mSampleRate, C2_PARAMKEY_SAMPLE_RATE)
                .withDefault(new C2StreamSampleRateInfo::input(0u, 44100))
                .withFields({C2F(mSampleRate, value).oneOf({
                    8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000
                })})
                .withSetter((Setter<decltype(*mSampleRate)>::StrictValueWithNoDeps))
                .build());

        addParameter(
                DefineParam(mChannelCount, C2_PARAMKEY_CHANNEL_COUNT)
                .withDefault(new C2StreamChannelCountInfo::input(0u, 2))
                .withFields({C2F(mChannelCount, value).oneOf({1, 2})})
                .withSetter(Setter<decltype(*mChannelCount)>::StrictValueWithNoDeps)
                .build());

        addParameter(
                DefineParam(mBitrate, C2_PARAMKEY_BITRATE)
                .withDefault(new C2StreamBitrateInfo::output(0u, 128000))
                .withFields({C2F(mBitrate, value).oneOf({
                    32000, 40000, 48000, 56000, 64000, 80000, 96000, 
                    112000, 128000, 160000, 192000, 224000, 256000, 320000
                })})
                .withSetter(Setter<decltype(*mBitrate)>::NonStrictValueWithNoDeps)
                .build());

        addParameter(
                DefineParam(mInputMaxBufSize, C2_PARAMKEY_INPUT_MAX_BUFFER_SIZE)
                .withDefault(new C2StreamMaxBufferSizeInfo::input(0u, 8192))
                .calculatedAs(MaxBufSizeCalculator, mChannelCount)
                .build());

        addParameter(
                DefineParam(mPcmEncoding, C2_PARAMKEY_PCM_ENCODING)
                .withDefault(new C2StreamPcmEncodingInfo::input(0u, C2Config::PCM_16))
                .withFields({C2F(mPcmEncoding, value).oneOf({
                    C2Config::PCM_16
                })})
                .withSetter(Setter<decltype(*mPcmEncoding)>::StrictValueWithNoDeps)
                .build());

            //  addParameter(
             //   DefineParam(mMp3Mode, "coded-audio.mp3.mode")
              //  .withDefault(new C2StreamTuning::input(0u, 1))  // 1 = stereo
              //  .withFields({C2F(mMp3Mode, value).inRange(0, 2)})
              //  .withSetter(Setter<decltype(*mMp3Mode)>::NonStrictValueWithNoDeps)
              //  .build());

        addParameter(
                DefineParam(mQuality, C2_PARAMKEY_QUALITY)
                .withDefault(new C2StreamQualityTuning::input(0u, 50))  // 0-100 scale
                .withFields({C2F(mQuality, value).inRange(0, 100)})
                .withSetter(Setter<decltype(*mQuality)>::NonStrictValueWithNoDeps)
                .build());
    }

    uint32_t getSampleRate() const { return mSampleRate->value; }
    uint32_t getChannelCount() const { return mChannelCount->value; }
    uint32_t getBitrate() const { return mBitrate->value; }
    uint32_t getPcmEncoding() const { return mPcmEncoding->value; }
   // uint32_t getMp3Mode() const { return mMp3Mode->value; }
    uint32_t getQuality() const { return mQuality->value; }

    static C2R MaxBufSizeCalculator(
            bool mayBlock,
            C2P<C2StreamMaxBufferSizeInfo::input> &me,
            const C2P<C2StreamChannelCountInfo::input> &channelCount) {
        (void)mayBlock;
        me.set().value = 1152 * sizeof(int16_t) * channelCount.v.value;
        return C2R::Ok();
    }

private:
    std::shared_ptr<C2StreamSampleRateInfo::input> mSampleRate;
    std::shared_ptr<C2StreamChannelCountInfo::input> mChannelCount;
    std::shared_ptr<C2StreamBitrateInfo::output> mBitrate;
    std::shared_ptr<C2StreamMaxBufferSizeInfo::input> mInputMaxBufSize;
    std::shared_ptr<C2StreamPcmEncodingInfo::input> mPcmEncoding;
  //  std::shared_ptr<C2StreamTuning::input> mMp3Mode;
    std::shared_ptr<C2StreamQualityTuning::input> mQuality;
};

// ==================== Constructor/Destructor ====================

C2SoftMp3Enc::C2SoftMp3Enc(
        const char *name,
        c2_node_id_t id,
        const std::shared_ptr<IntfImpl> &intfImpl)
    : SimpleC2Component(std::make_shared<SimpleInterface<IntfImpl>>(name, id, intfImpl)),
      mIntf(intfImpl),
      mLameEncoder(nullptr),
      mNumBytesPerInputFrame(0u),
      mOutBufferSize(0u),
      mSentCodecSpecificData(false),
      mInputSize(0),
      mSignalledError(false),
      mOutIndex(0u),
      mRemainderLen(0u),
      mSamplesPerFrame(1152),
      mNumChannels(0),
      mSampleRate(0),
      mBitrate(0) {
      ALOGI("Entered into the C2SoftMp3Enc Encoder");
}

C2SoftMp3Enc::~C2SoftMp3Enc() {
    onReset();
}

// ==================== Lifecycle Methods ====================

c2_status_t C2SoftMp3Enc::onInit() {
	ALOGI("onInit():checking encoder");
    status_t err = initEncoder();
    return err == OK ? C2_OK : C2_CORRUPTED;
}

status_t C2SoftMp3Enc::initEncoder() {
    // Initialize LAME encoder
    ALOGI("InitEncoder: Initializing the encoder");
    mLameEncoder = lame_init();
    if (mLameEncoder == nullptr) {
        ALOGE("Failed to initialize LAME encoder");
        return UNKNOWN_ERROR;
    }
    
    // Set LAME callbacks to null (no debug output)
    lame_set_errorf(mLameEncoder, nullptr);
    lame_set_debugf(mLameEncoder, nullptr);
    lame_set_msgf(mLameEncoder, nullptr);
    
    ALOGV("LAME encoder initialized successfully");
    return setAudioParams();
}

c2_status_t C2SoftMp3Enc::onStop() {
    mSentCodecSpecificData = false;
    mInputSize = 0u;
    mNextFrameTimestampUs.reset();
    mLastFrameEndTimestampUs.reset();
    mSignalledError = false;
    mRemainderLen = 0;
    return C2_OK;
}

void C2SoftMp3Enc::onReset() {
    (void)onStop();
    
    // Close LAME encoder
    if (mLameEncoder != nullptr) {
        lame_close(mLameEncoder);
        mLameEncoder = nullptr;
        ALOGV("LAME encoder closed");
    }
}

void C2SoftMp3Enc::onRelease() {
    // Ensure encoder is closed
    if (mLameEncoder != nullptr) {
        lame_close(mLameEncoder);
        mLameEncoder = nullptr;
    }
    
    // Clear remainder buffer
    mRemainderLen = 0;
    memset(mRemainder, 0, sizeof(mRemainder));
}

c2_status_t C2SoftMp3Enc::onFlush_sm() {
    mSentCodecSpecificData = false;
    mInputSize = 0u;
    mNextFrameTimestampUs.reset();
    mLastFrameEndTimestampUs.reset();
    mRemainderLen = 0;
    return C2_OK;
}

// ==================== Audio Parameter Setup ====================

// Helper to convert C2 MP3 mode to LAME mode
static MPEG_mode getLameMode(uint32_t channelCount) {
    // Simple logic: mono for 1 channel, joint stereo for 2 channels
    ALOGI("getlamemode: channelcount : %d",channelCount);
    if (channelCount == 1) {
        return MONO;
    } else {
        return JOINT_STEREO;
    }
}

status_t C2SoftMp3Enc::setAudioParams() {
    mSampleRate = mIntf->getSampleRate();
    mNumChannels = mIntf->getChannelCount();
    mBitrate = mIntf->getBitrate();
  //  uint32_t mp3Mode = mIntf->getMp3Mode();
    uint32_t quality = mIntf->getQuality();

    ALOGV("setAudioParams: %u Hz, %u channels, %u bps quality: %u",
         mSampleRate, mNumChannels, mBitrate, quality);

    // Determine MPEG version and frame size
    if (mSampleRate >= 32000) {
        mSamplesPerFrame = 1152;  // MPEG1
    } else {
        mSamplesPerFrame = 576;   // MPEG2/2.5
    }
    ALOGI("setAudioParams: samplesperframe : %d",mSamplesPerFrame);
    // Calculate input frame size in bytes
    mNumBytesPerInputFrame = mSamplesPerFrame * mNumChannels * sizeof(int16_t);
    
    // Calculate output buffer size (LAME suggests: 1.25 * samples_per_frame + 7200)
    mOutBufferSize = static_cast<uint32_t>(1.25 * mSamplesPerFrame + 7200);
    if (mOutBufferSize < 8192) {
        mOutBufferSize = 10000;  // Minimum buffer size
    }
    ALOGI("setAudioParams, outBuffersize : %d",mOutBufferSize);
    

    // Set LAME encoder parameters
    lame_set_in_samplerate(mLameEncoder, mSampleRate);
    lame_set_num_channels(mLameEncoder, mNumChannels);
    lame_set_brate(mLameEncoder, mBitrate / 1000);  // Convert bps to kbps
        // Use channel count to determine mode
    lame_set_mode(mLameEncoder, getLameMode(mNumChannels));
    lame_set_quality(mLameEncoder, quality);
    
    // Disable ID3 tag and VBR tag writing
   // lame_set_write_id3tag_automatic(mLameEncoder, 0);
   // lame_set_bWriteVbrTag(mLameEncoder, 0);
    
    // Use CBR mode (constant bitrate)
    lame_set_VBR(mLameEncoder, vbr_off);
    
    // Initialize encoder with parameters
    int ret = lame_init_params(mLameEncoder);
    if (ret < 0) {
        ALOGE("lame_init_params failed: %d", ret);
        return UNKNOWN_ERROR;
    }

    ALOGV("Audio params set: %u samples/frame, %u bytes input/frame, %u bytes output buffer",
         mSamplesPerFrame, mNumBytesPerInputFrame, mOutBufferSize);
    
    return OK;
}

// ==================== Encoding Helper Functions ====================

int C2SoftMp3Enc::encodeMonoFrame(const int16_t* pcm, uint8_t* mp3Buffer, size_t bufferSize) {
    // For mono, pass same buffer to left and right channels
    ALOGI("encodemonoframe");
    return lame_encode_buffer(
        mLameEncoder,
        pcm,                // left channel
        pcm,                // right channel (same as left for mono)
        mSamplesPerFrame,
        mp3Buffer,
        static_cast<int>(bufferSize)
    );
}

int C2SoftMp3Enc::encodeStereoFrame(const int16_t* pcm, uint8_t* mp3Buffer, size_t bufferSize) {
    // For stereo, we need to deinterleave
    ALOGI("encodestereoframe");
    int16_t left[mSamplesPerFrame];
    int16_t right[mSamplesPerFrame];
    
    // Deinterleave: pcm is [L0, R0, L1, R1, ...]
    for (size_t i = 0; i < mSamplesPerFrame; i++) {
        left[i] = pcm[i * 2];
        right[i] = pcm[i * 2 + 1];
    }
    
    return lame_encode_buffer(
        mLameEncoder,
        left,
        right,
        mSamplesPerFrame,
        mp3Buffer,
        static_cast<int>(bufferSize)
    );
}

int C2SoftMp3Enc::flushEncoder(uint8_t* mp3Buffer, size_t bufferSize) {
    return lame_encode_flush(
        mLameEncoder,
        mp3Buffer,
        static_cast<int>(bufferSize)
    );
}

void C2SoftMp3Enc::fillPcmBuffer(int16_t* pcmBuffer, size_t& samplesFilled) {
    if (mRemainderLen > 0) {
        size_t copySize = std::min(mRemainderLen, mSamplesPerFrame * mNumChannels * sizeof(int16_t));
        memcpy(pcmBuffer, mRemainder, copySize);
        samplesFilled = copySize / sizeof(int16_t);
        
        // Remove copied data from remainder
        memmove(mRemainder, mRemainder + copySize, mRemainderLen - copySize);
        mRemainderLen -= copySize;
    } else {
        samplesFilled = 0;
    }
}

void C2SoftMp3Enc::updateRemainder(const uint8_t* newData, size_t dataSize) {
    if (mRemainderLen + dataSize <= sizeof(mRemainder)) {
        memcpy(mRemainder + mRemainderLen, newData, dataSize);
        mRemainderLen += dataSize;
    } else {
        ALOGW("Remainder buffer overflow: %zu + %zu > %zu", 
              mRemainderLen, dataSize, sizeof(mRemainder));
        // Keep only what fits
        size_t spaceLeft = sizeof(mRemainder) - mRemainderLen;
        if (spaceLeft > 0) {
            memcpy(mRemainder + mRemainderLen, newData, spaceLeft);
            mRemainderLen = sizeof(mRemainder);
        }
    }
}

// ==================== Process Method ====================

static void MaybeLogTimestampWarning(
        long long lastFrameEndTimestampUs, long long inputTimestampUs) {
    using Clock = std::chrono::steady_clock;
    thread_local Clock::time_point sLastLogTimestamp{};
    thread_local int32_t sOverlapCount = -1;
    if (Clock::now() - sLastLogTimestamp > std::chrono::minutes(1) || sOverlapCount < 0) {
        AString countMessage = "";
        if (sOverlapCount > 0) {
            countMessage = AStringPrintf(
                    "(%d overlapping timestamp detected since last log)", sOverlapCount);
        }
        ALOGI("Correcting overlapping timestamp: last frame ended at %lldus but "
                "current frame is starting at %lldus. Using the last frame's end timestamp %s",
                lastFrameEndTimestampUs, inputTimestampUs, countMessage.c_str());
        sLastLogTimestamp = Clock::now();
        sOverlapCount = 0;
    } else {
        ALOGV("Correcting overlapping timestamp: last frame ended at %lldus but "
                "current frame is starting at %lldus. Using the last frame's end timestamp",
                lastFrameEndTimestampUs, inputTimestampUs);
        ++sOverlapCount;
    }
}

void C2SoftMp3Enc::process(
        const std::unique_ptr<C2Work> &work,
        const std::shared_ptr<C2BlockPool> &pool) {
    // Initialize output work
    work->result = C2_OK;
    work->workletsProcessed = 1u;
    work->worklets.front()->output.flags = work->input.flags;

    if (mSignalledError || mLameEncoder == nullptr) {
        work->result = C2_CORRUPTED;
        return;
    }
    
    bool eos = (work->input.flags & C2FrameData::FLAG_END_OF_STREAM) != 0;

    // Get input buffer
    uint8_t temp[1];
    C2ReadView view = mDummyReadView;
    const uint8_t *data = temp;
    size_t capacity = 0u;
    
    if (!work->input.buffers.empty()) {
        view = work->input.buffers[0]->data().linearBlocks().front().map().get();
        data = view.data();
        capacity = view.capacity();
    }
    
    // Handle timestamps
    c2_cntr64_t inputTimestampUs = work->input.ordinal.timestamp;
    if (inputTimestampUs < mLastFrameEndTimestampUs.value_or(inputTimestampUs)) {
        MaybeLogTimestampWarning(mLastFrameEndTimestampUs->peekll(), inputTimestampUs.peekll());
        inputTimestampUs = *mLastFrameEndTimestampUs;
    }
    
    if (capacity > 0) {
        if (!mNextFrameTimestampUs) {
            mNextFrameTimestampUs = work->input.ordinal.timestamp;
        }
        mLastFrameEndTimestampUs = inputTimestampUs
                + (capacity / sizeof(int16_t) * 1000000ll / mNumChannels / mSampleRate);
    }
    
    // Add new data to remainder
    if (capacity > 0) {
        updateRemainder(data, capacity);
    }
    
    // Calculate how many complete MP3 frames we can encode
    size_t numFrames = mRemainderLen / mNumBytesPerInputFrame;
    if (eos && (mRemainderLen % mNumBytesPerInputFrame) > 0) {
        numFrames++;  // Include partial frame at EOS
    }
    
    ALOGV("Process: eos=%d, remainder=%zu, frames=%zu, bytes/frame=%u",
          eos, mRemainderLen, numFrames, mNumBytesPerInputFrame);
    
    std::shared_ptr<C2LinearBlock> block;
    std::unique_ptr<C2WriteView> wView;
    uint8_t *outPtr = temp;
    size_t outAvailable = 0u;
    uint64_t inputIndex = work->input.ordinal.frameIndex.peeku();
    
    class FillWork {
    public:
        FillWork(uint32_t flags, C2WorkOrdinalStruct ordinal,
                 const std::shared_ptr<C2Buffer> &buffer)
            : mFlags(flags), mOrdinal(ordinal), mBuffer(buffer) {
        }
        
        void operator()(const std::unique_ptr<C2Work> &work) {
            work->worklets.front()->output.flags = (C2FrameData::flags_t)mFlags;
            work->worklets.front()->output.buffers.clear();
            work->worklets.front()->output.ordinal = mOrdinal;
            work->workletsProcessed = 1u;
            work->result = C2_OK;
            if (mBuffer) {
                work->worklets.front()->output.buffers.push_back(mBuffer);
            }
            ALOGV("timestamp = %lld, index = %lld, w/%s buffer",
                  mOrdinal.timestamp.peekll(),
                  mOrdinal.frameIndex.peekll(),
                  mBuffer ? "" : "o");
        }
        
    private:
        const uint32_t mFlags;
        const C2WorkOrdinalStruct mOrdinal;
        const std::shared_ptr<C2Buffer> mBuffer;
    };
    
    struct OutputBuffer {
        std::shared_ptr<C2Buffer> buffer;
        c2_cntr64_t timestampUs;
    };
    std::list<OutputBuffer> outputBuffers;
    
    // Encode each frame
    for (size_t frameIdx = 0; frameIdx < numFrames; frameIdx++) {
    	ALOGI("Process, frameIdx:%zu",frameIdx);
        if (!block) {
            C2MemoryUsage usage = { C2MemoryUsage::CPU_READ, C2MemoryUsage::CPU_WRITE };
            c2_status_t err = pool->fetchLinearBlock(mOutBufferSize, usage, &block);
            if (err != C2_OK) {
                ALOGE("fetchLinearBlock failed: %d", err);
                work->result = C2_NO_MEMORY;
                return;
            }
            
            wView.reset(new C2WriteView(block->map().get()));
            outPtr = wView->data();
            outAvailable = wView->size();
        }
        
        // Prepare PCM buffer for this frame
        int16_t pcmBuffer[mSamplesPerFrame * mNumChannels];
        size_t samplesNeeded = mSamplesPerFrame * mNumChannels;
        size_t bytesNeeded = samplesNeeded * sizeof(int16_t);
        ALOGI("mSamplesPerFrame=%d, mNumChannels=%d",mSamplesPerFrame, mNumChannels);
	ALOGI("samplesNeeded=%zu, bytesNeeded=%zu",samplesNeeded, bytesNeeded);
        
        // Check if we have enough data for a full frame
        bool isPartialFrame = (frameIdx == numFrames - 1) && 
                              (mRemainderLen < bytesNeeded) && eos;
                              
        
        if (isPartialFrame) {
        	ALOGI("Partial frame entered");
            // Partial frame at EOS - pad with zeros
            size_t bytesAvailable = mRemainderLen;
            size_t samplesAvailable = bytesAvailable / sizeof(int16_t);
            
            // Copy available data
            memcpy(pcmBuffer, mRemainder, bytesAvailable);
            // Pad with zeros
            memset(pcmBuffer + samplesAvailable, 0, bytesNeeded - bytesAvailable);
            
            // Clear remainder
            mRemainderLen = 0;
        } else {
            // Full frame
            ALOGI("Full frame");
            if (mRemainderLen >= bytesNeeded) {
                memcpy(pcmBuffer, mRemainder, bytesNeeded);
                // Remove consumed data
                memmove(mRemainder, mRemainder + bytesNeeded, mRemainderLen - bytesNeeded);
                mRemainderLen -= bytesNeeded;
            } else {
                break;  // Not enough data
            }
        }
        
        // Encode PCM to MP3
        int encodedBytes = 0;
        if (mNumChannels == 1) {
            encodedBytes = encodeMonoFrame(pcmBuffer, outPtr, outAvailable);
            ALOGI("process,outavailable : %zu ",outAvailable);
            
        } else {
            encodedBytes = encodeStereoFrame(pcmBuffer, outPtr, outAvailable);
            ALOGI("process,outavailable : %zu ",outAvailable);
        }
        
        if (encodedBytes > 0) {
            // Calculate timestamp for this frame
            c2_cntr64_t currentFrameTimestampUs = *mNextFrameTimestampUs;
            mNextFrameTimestampUs = currentFrameTimestampUs + 
            (c2_cntr64_t)((int64_t)mSamplesPerFrame * 1000000ll / mSampleRate);
            
            std::shared_ptr<C2Buffer> buffer = createLinearBuffer(block, 0, encodedBytes);
            outputBuffers.push_back({buffer, currentFrameTimestampUs});
            
            outPtr = temp;
            outAvailable = 0;
            block.reset();
            wView.reset();
        } else if (encodedBytes < 0) {
            ALOGE("MP3 encoding error: %d", encodedBytes);
            mSignalledError = true;
            work->result = C2_CORRUPTED;
            return;
        }
    }
    
    // Handle EOS - flush encoder
    if (eos) {
        if (!block) {
            C2MemoryUsage usage = { C2MemoryUsage::CPU_READ, C2MemoryUsage::CPU_WRITE };
            c2_status_t err = pool->fetchLinearBlock(mOutBufferSize, usage, &block);
            if (err == C2_OK) {
                wView.reset(new C2WriteView(block->map().get()));
                outPtr = wView->data();
                outAvailable = wView->size();
            }
        }
        
        if (outAvailable > 0) {
            int flushedBytes = flushEncoder(outPtr, outAvailable);
            if (flushedBytes > 0) {
                std::shared_ptr<C2Buffer> buffer = createLinearBuffer(block, 0, flushedBytes);
                c2_cntr64_t timestampUs = mNextFrameTimestampUs.value_or(work->input.ordinal.timestamp);
                outputBuffers.push_back({buffer, timestampUs});
                ALOGV("Flushed %d bytes at EOS", flushedBytes);
            }
        }
    }
    
    // Send output buffers
    while (outputBuffers.size() > 1) {
        const OutputBuffer& front = outputBuffers.front();
        C2WorkOrdinalStruct ordinal = work->input.ordinal;
        ordinal.frameIndex = mOutIndex++;
        ordinal.timestamp = front.timestampUs;
        cloneAndSend(
                inputIndex,
                work,
                FillWork(C2FrameData::FLAG_INCOMPLETE, ordinal, front.buffer));
        outputBuffers.pop_front();
    }
    
    std::shared_ptr<C2Buffer> buffer;
    C2WorkOrdinalStruct ordinal = work->input.ordinal;
    ordinal.frameIndex = mOutIndex++;
    if (!outputBuffers.empty()) {
        ordinal.timestamp = outputBuffers.front().timestampUs;
        buffer = outputBuffers.front().buffer;
    }
    
    // Mark the end of frame
    FillWork((C2FrameData::flags_t)(eos ? C2FrameData::FLAG_END_OF_STREAM : 0),
             ordinal, buffer)(work);
}

// ==================== Drain Method ====================

c2_status_t C2SoftMp3Enc::drain(
        uint32_t drainMode,
        const std::shared_ptr<C2BlockPool> &pool) {
    switch (drainMode) {
        case DRAIN_COMPONENT_NO_EOS:
            [[fallthrough]];
        case NO_DRAIN:
            return C2_OK;
        case DRAIN_CHAIN:
            return C2_OMITTED;
        case DRAIN_COMPONENT_WITH_EOS:
            break;
        default:
            return C2_BAD_VALUE;
    }

    (void)pool;
    mSentCodecSpecificData = false;
    mInputSize = 0u;
    mNextFrameTimestampUs.reset();
    mLastFrameEndTimestampUs.reset();
    mRemainderLen = 0;

    return C2_OK;
}

// ==================== Factory ====================

class C2SoftMp3EncFactory : public C2ComponentFactory {
public:
    C2SoftMp3EncFactory() : mHelper(std::static_pointer_cast<C2ReflectorHelper>(
            GetCodec2PlatformComponentStore()->getParamReflector())) {
    }

    virtual c2_status_t createComponent(
            c2_node_id_t id,
            std::shared_ptr<C2Component>* const component,
            std::function<void(C2Component*)> deleter) override {
        *component = std::shared_ptr<C2Component>(
                new C2SoftMp3Enc(COMPONENT_NAME,
                                 id,
                                 std::make_shared<C2SoftMp3Enc::IntfImpl>(mHelper)),
                deleter);
        return C2_OK;
    }

    virtual c2_status_t createInterface(
            c2_node_id_t id, std::shared_ptr<C2ComponentInterface>* const interface,
            std::function<void(C2ComponentInterface*)> deleter) override {
        *interface = std::shared_ptr<C2ComponentInterface>(
                new SimpleInterface<C2SoftMp3Enc::IntfImpl>(
                        COMPONENT_NAME, id, std::make_shared<C2SoftMp3Enc::IntfImpl>(mHelper)),
                deleter);
        return C2_OK;
    }

    virtual ~C2SoftMp3EncFactory() override = default;

private:
    std::shared_ptr<C2ReflectorHelper> mHelper;
};

}  // namespace android

__attribute__((cfi_canonical_jump_table))
extern "C" ::C2ComponentFactory* CreateCodec2Factory() {
    ALOGV("in %s", __func__);
    return new ::android::C2SoftMp3EncFactory();
}

__attribute__((cfi_canonical_jump_table))
extern "C" void DestroyCodec2Factory(::C2ComponentFactory* factory) {
    ALOGV("in %s", __func__);
    delete factory;
}
