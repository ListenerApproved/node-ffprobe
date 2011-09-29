var spawn = require('child_process').spawn,
		path = require('path');

module.exports = (function() {
	function parseBlock(block) {
		var block_object = {}, lines = block.split('\n');

		lines.forEach(function(line) {
			var data = line.split('=');
			if(data && data.length === 2) block_object[data[0]] = convert(data[1].trim());
		});

		return block_object;
	};

	function convert(str) {
		return str.match(/^\d+\.?\d*$/) ? parseFloat(str) : str;
	};

	function parseStreams(text, callback) {
		var start = text.indexOf('[STREAM]'),
				end		= text.lastIndexOf('[/STREAM]');

		if(start === -1 || end === -1) return { streams: null };

		var streams = [];
		var parsed = text.slice(start + 8, end).trim();
		parsed = parsed.replace('[STREAM]\n', '');

		blocks = parsed.split('[/STREAM]');

		blocks.forEach(function(stream, idx) {
			var codec_data = parseBlock(stream);
			var sindex = codec_data.index;
			delete codec_data.index;

			if(sindex) streams[sindex] = codec_data;
			else streams.push(codec_data);
		});

		return { streams: streams };
	};

	function parseFormat(text, callback) {
		var start = text.indexOf('[FORMAT]'),
				end		= text.lastIndexOf('[/FORMAT]');

		if(start === -1 || end === -1) return { format: null }

		var parsed = text.slice(start + 8, end).trim();
		block = parsed.replace('[FORMAT]\n', '').replace('[/FORMAT]', '');
		var raw_format = parseBlock(block), format = { }, metadata = { };

		//REMOVE metadata
		delete raw_format.filename;
		for(var attr in raw_format) {
			if(raw_format.hasOwnProperty(attr)) {
				if(attr.indexOf('TAG') === -1) format[attr] = raw_format[attr];
				else metadata[attr.slice(4)] = raw_format[attr];
			}
		}

		return { format: format, metadata: metadata };
	};

	return function(file, callback) {
		var proc = spawn('ffprobe', ['-show_streams', '-show_format', '-convert_tags', file]), probeData = []
		var start = Date.now();

		proc.stdout.on('data', function(data) { probeData.push(data.toString()) });

		proc.on('exit', function() {
			var pdata = probeData.join('');
			var s = parseStreams(pdata), f = parseFormat(pdata);

			callback({
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
})()
