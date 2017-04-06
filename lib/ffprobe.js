var spawn = require('child_process').spawn,
		fs = require('fs'),
		path = require('path');

module.exports = (function() {
	function findBlocks(raw) {
		var stream_start = raw.indexOf('[STREAM]') + 8,
				stream_end = raw.lastIndexOf('[/STREAM]'),
				format_start = raw.indexOf('[FORMAT]') + 8,
				format_end = raw.lastIndexOf('[/FORMAT]');

		var blocks = { streams: null, format: null };

		if(stream_start !== 7 && stream_end !== -1) {
			blocks.streams = raw.slice(stream_start, stream_end).trim();
		}

		if(format_start !== 7 && format_end !== -1) {
			blocks.format = raw.slice(format_start, format_end).trim();
		}

		return blocks;
	};


	function parseField(str) {
		str = ("" + str).trim();
		return str.match(/^\d+\.?\d*$/) ? parseFloat(str) : str;
	};

	function parseBlock(block) {
		var block_object = {}, lines = block.split('\n');

		lines.forEach(function(line) {
			var data = line.split('=');
			if(data && data.length === 2) {
				block_object[data[0]] = parseField(data[1]);
			}
		});

		return block_object;
	};

	function extractMetadata(raw_data, metadata, other) {
		for(var attr in raw_data) {
			if(raw_data.hasOwnProperty(attr)) {
				if(attr.indexOf('TAG') === -1) other[attr] = raw_data[attr];
				else metadata[attr.slice(4)] = raw_data[attr];
			}
		}
	}

	function parseStreams(text, callback) {
		if(!text) return { streams: null };

		var streams = [];
		var streamsMetadata = [];
		var blocks = text.replace('[STREAM]\n', '').split('[/STREAM]');

		blocks.forEach(function(stream, idx) {
			var codec_data = parseBlock(stream);
			var sindex = codec_data.index;
			delete codec_data.index;

			var stream = { },
					metadata = { };

			extractMetadata(codec_data, metadata, stream);

			if(sindex) {
				streams[sindex] = stream;
				streamsMetadata[sindex] = metadata;
			}
			else {
				streams.push(codec_data);
				streamsMetadata.push(metadata);
			}
		});

		return { streams: streams, metadata: streamsMetadata };
	};

	function parseFormat(text, callback) {
		if(!text) return { format: null }

		var block = text.replace('[FORMAT]\n', '').replace('[/FORMAT]', '');

		var raw_format = parseBlock(block),
				format = { },
				metadata = { };

		//REMOVE metadata
		delete raw_format.filename;
		extractMetadata(raw_format, metadata, format);

		return { format: format, metadata: metadata };
	};

	function doProbe(file, callback) {
		var proc = spawn(module.exports.FFPROBE_PATH || 'ffprobe', ['-show_streams', '-show_format', '-loglevel', 'warning', file]),

				probeData = [],
				errData = [],
				exitCode = null,
				start = Date.now();

		proc.stdout.setEncoding('utf8');
		proc.stderr.setEncoding('utf8');

		proc.stdout.on('data', function(data) { probeData.push(data) });
		proc.stderr.on('data', function(data) { errData.push(data) });

		proc.on('exit', function(code) {
			exitCode = code;
		});
		proc.on('error', function(err) {
			callback(err);
		});
		proc.on('close', function() {
			var blocks = findBlocks(probeData.join(''));

			var s = parseStreams(blocks.streams),
					f = parseFormat(blocks.format);

			if (exitCode) {
				var err_output = errData.join('');
				return callback(err_output);
			}

			f.metadata.streams = s.metadata;

			callback(null, {
				filename: path.basename(file),
				filepath: path.dirname(file),
				fileext: path.extname(file),
				file: file,
				probe_time: Date.now() - start,
				streams: s.streams,
				format: f.format,
				metadata: f.metadata
			});
		});
	};

	return doProbe;
})()
