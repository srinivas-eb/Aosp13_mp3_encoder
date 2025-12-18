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
