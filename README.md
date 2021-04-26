# DS-Subtitler

Generate SRT subtitles from libsndfile supported audio files using Mozilla DeepSpeech.

## Requires

* deepspeech
* sndfile
* libresample
* rnnoise

## Usage

```
-b beamwidth    Beam width
-m model	Model to use, default is deepspeech-models.pbmm
-s scorer	Scorer to use, default is deepspeech-models.scorer
-w words.txt	Read hotwords from file
-f 2		Frames to use for silence detection for splitting
-l 4		Minimum length in seconds to decode
-r              Use raw audio for DS
```

## Issues

* The code is uggly, very ugly
* 2-channel files are not handled properly yet
