FFProbe for NodeJS
==========

A simple wrapper around ffprobe written in NodeJS

***This module requires ffmpeg to be installed before it can function***.  The ffmpeg package comes bundled with ffprobe.

Installation
----------

    sudo apt-get install ffmpeg
    npm install node-ffprobe

Usage
----------

```js
var probe = require('node-ffprobe');

var track = '/path/to/media/file.mp3';

probe(track, function(probeData) {
	console.log(probeData);
});
```

Calling probe will execute ffprobe and parse the data it sends to STDOUT.  A sample object can be seen below.

The ***streams***, ***format***, and ***metadata*** fields are taken directly from ffprobe.
***probe_time*** is the total execution time for the given file.

```js
{
  "filename": "15 Fsosf.mp3",
  "filepath": "/path/to/media",
  "file": "/path/to/media/15 Fsosf.mp3",
  "probe_time": 28,
  "streams": [
    {
      "codec_name": "mp3",
      "codec_long_name": "MP3 (MPEG audio layer 3)",
      "codec_type": "audio",
      "codec_time_base": "0/1",
      "codec_tag_string": "[0][0][0][0]",
      "codec_tag": "0x0000",
      "sample_rate": "44100.000000",
      "channels": "2",
      "bits_per_sample": "0",
      "r_frame_rate": "0/0",
      "avg_frame_rate": "1225/32",
      "time_base": "1/14112000",
      "start_time": "0.000000",
      "duration": "552.641750"
    }
  ],
  "format": {
    "nb_streams": "1",
    "format_name": "mp3",
    "format_long_name": "MPEG audio layer 2/3",
    "start_time": "0.000000",
    "duration": "552.641750",
    "size": "8842268.000000",
    "bit_rate": "128000.000000"
  },
  "metadata": {
    "title": "Fsosf",
    "artist": "Bassnectar",
    "album": "Underground Communication",
    "date": "2007",
    "track": "15.000000"
  }
}
```
