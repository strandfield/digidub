
# digidub

A Qt-based command-line program for dubbing a video with audio from another video.

This program was written for dubbing episodes of the "Digimon: Digital Monsters Season 1-4" DVD boxset
with audio from the french release.

The goal of the program is simple:
- take an episode (dubbed in english) from the series
- take the same episode from the french release
- produce a video that uses the "english" video and the french audio

Which translates to the following command-line invocation of the program:
```bash
digidub dub eng.mkv --with fre.mkv -o output.mkv
```

Now, that sounds easy on paper because "this is the same episode", but 
it turned out to be far more challenging in practice.

Different releases have different requirements.

For example, all episodes in the french release are about 20 minutes and 
30 seconds long (my guess is that it is a TV release) ; while episodes
from the english release ranges from 20:33 to less than 19 minutes.

Some episodes from the french release contains additional or removed 
material. Some even slow down the video to be able to reach the 
target duration with the same material.

`digidub` tries to do a good job on its own by performing silence, black
frame and scene change detection on the video to dub before trying to 
find matching scenes in the second video. <br/>
Extra information can be provided through the command-line to help
the program when it struggles to produce a good output:
- `--exclude` can be used to exclude some part of the video from the 
  dubbing process
- `--reusable` can be used to mark some part of the second video that
  can be used more than once
- `--force-match` can be used to force a match between two parts in the
  videos.


## Requirements

For compiling:
- Qt 6
- a compiler supporting C++

For running the program:
- FFmpeg and mkvmerge must be installed on your system.
