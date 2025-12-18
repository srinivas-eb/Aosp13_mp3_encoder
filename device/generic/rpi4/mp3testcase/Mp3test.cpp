#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <android/log.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <vector>
#include <string>
#include <errno.h>

#define LOG_TAG "mp3_encoder_test"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Minimal WAV header parsing (PCM only)
struct WavHeader {
    uint32_t riff;       // "RIFF"
    uint32_t size;
    uint32_t wave;       // "WAVE"
    uint32_t fmt;        // "fmt "
    uint32_t fmtSize;    // 16 for PCM
    uint16_t audioFormat;// 1 = PCM
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
    uint32_t dataTag;    // "data"
    uint32_t dataSize;
};

bool readWavHeader(FILE* f, WavHeader* h) {
    if (fread(h, sizeof(WavHeader), 1, f) != 1) return false;
    if (h->riff != 0x46464952 /*RIFF*/ || h->wave != 0x45564157 /*WAVE*/) return false;
    if (h->fmt != 0x20746d66 /*fmt */ || h->audioFormat != 1) return false;
    if (h->bitsPerSample != 16) return false; // assume 16-bit PCM
    if (h->dataTag != 0x61746164 /*data*/) {
        // Skip extra chunks until "data"
        uint32_t tag = h->dataTag;
        uint32_t size = h->dataSize;
        while (tag != 0x61746164) {
            if (fseek(f, size, SEEK_CUR) != 0) return false;
            if (fread(&tag, sizeof(uint32_t), 1, f) != 1) return false;
            if (fread(&size, sizeof(uint32_t), 1, f) != 1) return false;
        }
        h->dataTag = tag;
        h->dataSize = size;
    }
    return true;
}

// MP3 doesn't need ADTS headers, so we'll just write raw MP3 frames
// MP3 encoder typically produces MPEG-1 Layer III frames

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: mp3_encoder_test <input.wav> <output.mp3>\n");
        return 1;
    }
    const char* inPath = argv[1];
    const char* outPath = argv[2];

    FILE* fin = fopen(inPath, "rb");
    if (!fin) {
        LOGE("Failed to open input: %s (errno=%d)", inPath, errno);
        return 2;
    }
    WavHeader wh;
    if (!readWavHeader(fin, &wh)) {
        LOGE("Invalid or unsupported WAV header");
        fclose(fin);
        return 3;
    }
    LOGI("WAV: %u Hz, %u ch, %u bits, data=%u bytes",
         wh.sampleRate, wh.numChannels, wh.bitsPerSample, wh.dataSize);

    // MP3 encoder typically supports specific sample rates
    // Common MP3 sample rates: 44100, 48000, 32000, 24000, 22050, 16000, 12000, 11025, 8000
    if (wh.sampleRate > 48000) {
        LOGI("Warning: Sample rate %u Hz may be reduced to 48kHz by MP3 encoder", wh.sampleRate);
    }

    FILE* fout = fopen(outPath, "wb");
    if (!fout) {
        LOGE("Failed to open output: %s (errno=%d)", outPath, errno);
        fclose(fin);
        return 4;
    }

    // Configure MP3 encoder
    AMediaFormat* fmt = AMediaFormat_new();
    AMediaFormat_setString(fmt, AMEDIAFORMAT_KEY_MIME, "audio/mpeg");
    
    // MP3 encoder typically supports specific sample rates
    // Use the input sample rate, encoder will handle conversion if needed
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_SAMPLE_RATE, (int32_t)wh.sampleRate);
    
    // MP3 supports mono (1) and stereo (2) channels
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, (int32_t)wh.numChannels);
    
    // Bit rate - common MP3 bit rates: 32000, 64000, 96000, 128000, 192000, 256000, 320000
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_BIT_RATE, 128000);
    
    // Optional: Set MP3 specific parameters if supported by the encoder
    // AMediaFormat_setInt32(fmt, "layer", 3); // Layer III
    // AMediaFormat_setInt32(fmt, "version", 1); // MPEG-1 (1 for MPEG-1, 2 for MPEG-2)
    
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_MAX_INPUT_SIZE, 8192);

    AMediaCodec* codec = AMediaCodec_createEncoderByType("audio/mpeg");
    if (!codec) {
        LOGE("Failed to create MP3 encoder. This might mean MP3 encoder is not available on this device.");
        // Try alternative MIME type
        codec = AMediaCodec_createEncoderByType("audio/mpeg-L3");
        if (!codec) {
            LOGE("Failed to create MP3 encoder with alternative MIME type as well.");
            AMediaFormat_delete(fmt);
            fclose(fin);
            fclose(fout);
            return 5;
        } else {
            LOGI("Created MP3 encoder with alternative MIME type 'audio/mpeg-L3'");
        }
    }
    
    LOGI("Configuring MP3 encoder with: %s", AMediaFormat_toString(fmt));
    
    media_status_t st = AMediaCodec_configure(codec, fmt, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
    if (st != AMEDIA_OK) {
        LOGE("configure failed: %d", st);
        AMediaCodec_delete(codec);
        AMediaFormat_delete(fmt);
        fclose(fin);
        fclose(fout);
        return 6;
    }
    
    st = AMediaCodec_start(codec);
    if (st != AMEDIA_OK) {
        LOGE("start failed: %d", st);
        AMediaCodec_delete(codec);
        AMediaFormat_delete(fmt);
        fclose(fin);
        fclose(fout);
        return 7;
    }

    const size_t frameBytes = wh.blockAlign; // bytes per PCM frame (all channels)
    std::vector<uint8_t> pcmBuf(8192);
    bool eosSent = false;
    uint64_t totalInputBytes = 0;
    uint64_t totalOutputBytes = 0;
    int frameCount = 0;

    LOGI("Starting encoding loop...");

    while (true) {
        ssize_t inIx = AMediaCodec_dequeueInputBuffer(codec, 10000 /*us*/);
        if (inIx >= 0) {
            size_t inSize = 0;
            uint8_t* inPtr = AMediaCodec_getInputBuffer(codec, inIx, &inSize);
            if (!eosSent) {
                size_t toRead = pcmBuf.size();
                // read up to buffer size but aligned to frameBytes
                toRead = (toRead / frameBytes) * frameBytes;
                size_t readCount = fread(pcmBuf.data(), 1, toRead, fin);
                if (readCount == 0) {
                    LOGI("End of input reached, sending EOS to encoder");
                    AMediaCodec_queueInputBuffer(codec, inIx, 0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                    eosSent = true;
                } else {
                    if (readCount > inSize) readCount = inSize;
                    memcpy(inPtr, pcmBuf.data(), readCount);
                    totalInputBytes += readCount;
                    
                    // Simple PTS: assumes constant sample rate
                    static uint64_t ptsUs = 0;
                    uint32_t samples = (readCount / (wh.numChannels * (wh.bitsPerSample / 8)));
                    uint64_t deltaUs = (uint64_t)samples * 1000000ULL / wh.sampleRate;
                    AMediaCodec_queueInputBuffer(codec, inIx, 0, readCount, ptsUs, 0);
                    ptsUs += deltaUs;
                    
                    if (totalInputBytes % (wh.sampleRate * wh.numChannels * 2) == 0) {
                        LOGI("Processed %llu bytes of input", (unsigned long long)totalInputBytes);
                    }
                }
            } else {
                AMediaCodec_queueInputBuffer(codec, inIx, 0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
            }
        }

        AMediaCodecBufferInfo info;
        ssize_t outIx = AMediaCodec_dequeueOutputBuffer(codec, &info, 10000 /*us*/);
        while (outIx >= 0) {
            size_t outSize = 0;
            uint8_t* outPtr = AMediaCodec_getOutputBuffer(codec, outIx, &outSize);
            if (info.size > 0 && outPtr) {
                // MP3 doesn't need headers like ADTS for AAC
                // Just write the raw MP3 frames
                fwrite(outPtr + info.offset, 1, info.size, fout);
                totalOutputBytes += info.size;
                frameCount++;
                
                if (frameCount % 100 == 0) {
                    LOGI("Encoded frame %d, size=%d bytes, total output=%llu bytes", 
                         frameCount, info.size, (unsigned long long)totalOutputBytes);
                }
            }
            AMediaCodec_releaseOutputBuffer(codec, outIx, false);
            if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
                LOGI("EOS received from encoder");
                goto done;
            }
            outIx = AMediaCodec_dequeueOutputBuffer(codec, &info, 0);
        }
        if (outIx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            AMediaFormat* ofmt = AMediaCodec_getOutputFormat(codec);
            LOGI("Output format changed: %s", AMediaFormat_toString(ofmt));
            
            // Log specific MP3 parameters if available
            int32_t sampleRate, channels, bitrate;
            if (AMediaFormat_getInt32(ofmt, AMEDIAFORMAT_KEY_SAMPLE_RATE, &sampleRate)) {
                LOGI("Output sample rate: %d Hz", sampleRate);
            }
            if (AMediaFormat_getInt32(ofmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &channels)) {
                LOGI("Output channels: %d", channels);
            }
            if (AMediaFormat_getInt32(ofmt, AMEDIAFORMAT_KEY_BIT_RATE, &bitrate)) {
                LOGI("Output bitrate: %d bps", bitrate);
            }
            
            AMediaFormat_delete(ofmt);
        }
        if (outIx == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
            LOGI("Output buffers changed");
        }
        if (outIx == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
            // This is normal, just means no output buffer is ready yet
        }
    }

done:
    LOGI("Encoding complete. Stopping codec...");
    AMediaCodec_stop(codec);
    AMediaCodec_delete(codec);
    AMediaFormat_delete(fmt);
    fclose(fin);
    fclose(fout);
    
    LOGI("Done. Wrote MP3 to %s", outPath);
    LOGI("Statistics: Input=%llu bytes, Output=%llu bytes, Frames=%d, Compression ratio=%.2f:1",
         (unsigned long long)totalInputBytes,
         (unsigned long long)totalOutputBytes,
         frameCount,
         totalInputBytes > 0 ? (float)totalInputBytes / totalOutputBytes : 0.0f);
    return 0;
}
