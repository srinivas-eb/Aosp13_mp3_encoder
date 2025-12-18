
# AOSP13_mp3_encoder

MP3 encoder (LAME) integration into Android AOSP 13 using Codec2 framework.

## Overview

This repository documents the complete process of integrating the LAME MP3 encoder into Android AOSP 13 using the Codec2 framework. The implementation includes:

- LAME MP3 encoder as the backend encoding engine
- Codec2 software encoder wrapper
- Tested on AOSP 13 running on Raspberry Pi 4
- Integration with Android's media framework

## Prerequisites

- AOSP 13 source code
- Basic understanding of Android build system
- Familiarity with Codec2 framework

## Integration Steps

### 1. Download and Build LAME

Clone the LAME repository from the official source:

```bash
git clone https://github.com/rbrito/lame.git
cd lame
./configure
make
```

This builds both static and shared libraries which will be used by the Codec2 MP3 encoder wrapper.

### 2. Add LAME to AOSP Source

Copy the LAME source to AOSP's external directory (where third-party sources are typically placed):

```bash
cp -r lame  <AOSP_ROOT>/external/
```

### 3. Create Build Configuration

Add an `Android.bp` file in the lame directory to define the build rules. Make sure to enable APEX availability if needed.

### 4. Project Structure

After integration, your project structure should look like this:

```
├── README.md
├── external/
│   └── lame/
│       └── Android.bp
├── frameworks/
│   └── av/
│       ├── media/
│       │   ├── codec2/
│       │   │   └── components/
│       │   │       └── mp3enc/
│       │   │           ├── Android.bp
│       │   │           ├── C2SoftMp3Enc.cpp
│       │   │           └── C2SoftMp3Enc.h
│       │   └── libstagefright/
│       │       └── data/
│       │           ├── media_codecs_sw.xml
│       │           └── media_codecs_c2_audio.xml
│       ├── codec2/
│       │   └── vndk/
│       │       └── c2store.cpp
│       └── service/
│           └── mediacodec/
│               └── registrant/
│                   └── Android.bp
```

### 5. Register the Encoder

After building, you'll get an `.so` file from the mp3enc directory. Register this library by updating:

1. `frameworks/av/service/mediacodec/registrant/Android.bp`
2. `frameworks/av/codec2/vndk/c2store.cpp`

## Testing the Integration

### Writing Test Cases

Create test cases using NDK MediaCodec APIs. You'll need:

1. A `.bp` file to build the test
2. A `.cpp` file implementing the test using MediaCodec

### Running Tests

1. Build and push the test binary to the device:
```bash
adb push mp3encodetest /system/local/tmp/
```

2. Execute the test with input and output paths:
```bash
adb shell
cd /system/local/tmp
./mp3encodetest /sdcard/input.wav /sdcard/output.mp3
```

3. Pull the encoded file from the device:
```bash
adb pull /sdcard/output.mp3 .
```

**Note:** These commands work for both emulators and physical devices (like Raspberry Pi) connected via ADB.

## Building AOSP

After all changes, build AOSP as usual:

```bash
source build/envsetup.sh
lunch <your_target>
make -j$(nproc)
```

## Verification

To verify the encoder is properly integrated:

1. Check if the MP3 encoder appears in media codec list:
```bash
adb shell dumpsys media.player | grep mp3enc
```

2. Verify the encoder component is loaded:
```bash
adb logcat | grep -i "mp3|C2SoftMp3Enc"
```

## Troubleshooting

- Ensure all file permissions are set correctly
- Verify the encoder is listed in appropriate XML configuration files
- Check build logs for any missing dependencies
- Confirm the LAME library is correctly linked

## Contributing

Feel free to submit issues and pull requests for improvements or bug fixes.

## License

This integration uses LAME which is licensed under LGPL. Please ensure compliance with the license terms when using this code.
```

This improved version:
1. Has a clearer structure with proper sections
2. Includes verification steps
3. Adds troubleshooting section
4. Uses consistent formatting
5. Provides better context for each step
6. Includes license considerations
7. Has a more professional tone while keeping your original content
