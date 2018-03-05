FFProbe for NodeJS
==========

A simple wrapper around ffprobe written in NodeJS

***This module requires ffmpeg to be installed before it can function***.  The ffmpeg package comes bundled with ffprobe.

Installation
----------

   Add to `package.json`
   
   
   ```js
     "dependencies": {
   
        "node-ffprobe": "git+ssh://git@github.com:ListenerApproved/node-ffprobe.git#master",
      
        "@ffprobe-installer/ffprobe": "^1.0.8"
      
     }
   ```
    
    
    


Run
----------
 	$ npm i



Usage
----------

```js
const ffprobe = require('node-ffprobe');
const ffprobeInstaller = require('@ffprobe-installer/ffprobe');
console.log(ffprobeInstaller.path, ffprobeInstaller.version);

ffprobe.FFPROBE_PATH = ffprobeInstaller.path;

var track = '/path/to/media/file.mp3'; // or video

probe(track, (err, probeData) => {
	console.log(probeData);
});
```
FFPROBE_PATH is useful if you embed the lib in your app.

Calling probe will execute ffprobe and parse the data it sends to STDOUT.  A sample object can be seen below.

The ***streams***, ***format***, and ***metadata*** fields are taken directly from ffprobe.
***probe_time*** is the total execution time for the given file.

```js
{
	"filename": "Before We Dissolve.mp3",
	"filepath": "/path/to/media",
	"fileext": ".mp3",
	"file": "/path/to/media/Before We Dissolve.mp3",
	"probe_time": 642,
	"streams": [
		{
			"codec_name": "mp3",
			"codec_long_name": "MP3 (MPEG audio layer 3)",
			"codec_type": "audio",
			"codec_time_base": "0/1",
			"codec_tag_string": "[0][0][0][0]",
			"codec_tag": "0x0000",
			"sample_rate": 44100,
			"channels": 2,
			"bits_per_sample": 0,
			"r_frame_rate": "0/0",
			"avg_frame_rate": "1225/32",
			"time_base": "1/14112000",
			"start_time": 0,
			"duration": 149.524898
		}
	],
	"format": {
		"nb_streams": 1,
		"format_name": "mp3",
		"format_long_name": "MPEG audio layer 2/3",
		"start_time": 0,
		"duration": 149.524898,
		"size": 2392815,
		"bit_rate": 128022
	},
	"metadata": {}
}
```
