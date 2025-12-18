# Aosp13_mp3_encoder
MP3 encoder (LAME) integration into Android AOSP 13 using Codec2
# MP3 Encoder Integration in AOSP 13

This repository documents the steps and source changes required to integrate
the LAME MP3 encoder into Android AOSP 13 using the Codec2 framework.

## Overview
- LAME MP3 encoder is used as the backend
- Integrated as a Codec2 software encoder
- Tested on AOSP 13 (Raspberry Pi 4)
- 
### Download LAME

LAME encoder is downloaded from the official GitHub repository:

https://github.com/rbrito/lame

Clone the repository:

```bash
git clone https://github.com/rbrito/lame.git

##Build LAME
cd lame
./configure
make
This builds the LAME static and shared libraries which are later used
by the Codec2 MP3 encoder wrapper.

After this add the lame into the AOSP source file
/externel/

Because externel directory is used to keep the third party sources,

Add the bp file in side the lame directory
So for the Bp file enable the apex available,


below gave the tree
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


After completing the build,you will get an (.so) from the mp3enc file directory.
So add the (.so) at below bp files.
frameworks/av/service/mediacodec/registrent/Android.bp
frameworks/media/codec2/vndk/c2store.cpp


##for testing you encoder is properly integrated or not.You need to write the testcase.


