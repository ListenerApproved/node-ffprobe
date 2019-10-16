# FFProbe for NodeJS

A simple sync wrapper around ffprobe written in NodeJS

***This module requires ffmpeg to be installed before it can function***.  The ffprobe utility comes bundled with ffmpeg.  If you just need this to work under multiple configs consider using `@ffprobe-installer/ffprobe` as detailed in the usage example below

## Installation

```sh
# regular NPM package install
npm install --save node-ffprobe

# install directly from GitHub
npm install --save github:ListenerApproved/node-ffprobe

# install NPM package via Yarn
yarn add node-ffprobe
```



## Run
 	$ npm i



## Usage

```js
const probe = require('node-ffprobe')
const ffprobeInstaller = require('@ffprobe-installer/ffprobe')

console.log(ffprobeInstaller.path, ffprobeInstaller.version)

ffprobe.FFPROBE_PATH = ffprobeInstaller.path

var track = '/path/to/media/file.mp3' // or video

probe(track).then(probeData => {
	console.log(probeData)
})
```

FFPROBE_PATH is useful if you embed the lib in your app.

Calling probe will execute ffprobe and parse the data it sends to STDOUT.  A sample object can be seen below.

Additionnally, you can set `ffprobe.SYNC` to `true` if you want for a particular reason to launch ffprobe synchronously (for example when used in batch processing of files to avoid too many spawns at once.)

The JSON returned by this utility is directly produced by ffprobe using the `-print_format json` flag


### Example Output


```js
{
   "programs": [],
   "streams": [
      {
         "index": 0,
         "codec_name": "mp3",
         "codec_long_name": "MP3 (MPEG audio layer 3)",
         "codec_type": "audio",
         "codec_time_base": "1/44100",
         "codec_tag_string": "[0][0][0][0]",
         "codec_tag": "0x0000",
         "sample_fmt": "fltp",
         "sample_rate": "44100",
         "channels": 2,
         "channel_layout": "stereo",
         "bits_per_sample": 0,
         "r_frame_rate": "0/0",
         "avg_frame_rate": "0/0",
         "time_base": "1/14112000",
         "start_pts": 353600,
         "start_time": "0.025057",
         "duration_ts": 1865687040,
         "duration": "132.205714",
         "bit_rate": "320000",
         "disposition": {
            "default": 0,
            "dub": 0,
            "original": 0,
            "comment": 0,
            "lyrics": 0,
            "karaoke": 0,
            "forced": 0,
            "hearing_impaired": 0,
            "visual_impaired": 0,
            "clean_effects": 0,
            "attached_pic": 0,
            "timed_thumbnails": 0
         },
         "tags": {
            "encoder": "LAME3.99r"
         }
      }
   ],
   "chapters": [],
   "format": {
      "filename": "/Users/im.a.little.teapot/Downloads/file_example_MP3_5MG.mp3",
      "nb_streams": 1,
      "nb_programs": 0,
      "format_name": "mp3",
      "format_long_name": "MP2/3 (MPEG audio layer 2/3)",
      "start_time": "0.025057",
      "duration": "132.205714",
      "size": "5289384",
      "bit_rate": "320069",
      "probe_score": 51,
      "tags": {
         "genre": "Cinematic",
         "album": "YouTube Audio Library",
         "title": "Impact Moderato",
         "artist": "Kevin MacLeod"
      }
   }
}
```


## Roadmap
* Timeouts for async invocation
* move package to unified nodejs ffmpeg library 
