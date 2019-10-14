const spawn = require('child_process').spawn
const spawnSync = require('child_process').spawnSync

module.exports = (function () {
	function doProbeSync(file) {
		let proc = spawnSync(module.exports.FFPROBE_PATH || 'ffprobe', ['-hide_banner', '-loglevel', 'fatal', '-show_error', '-show_format', '-show_streams', '-show_programs', '-show_chapters', '-show_private_data', '-print_format', 'json', file], { encoding: 'utf8' })
		let probeData = []
		let errData = []
		let exitCode = null

		probeData.push(proc.stdout)
		errData.push(proc.stderr)

		exitCode = proc.status

		if (proc.error) throw new Error(proc.error)
		if (exitCode) throw new Error(errData.join(''))

		return JSON.parse(probeData.join(''))
	}

	function doProbe(file) {
		return new Promise((resolve, reject) => {
			let proc = spawn(module.exports.FFPROBE_PATH || 'ffprobe', ['-hide_banner', '-loglevel', 'fatal', '-show_error', '-show_format', '-show_streams', '-show_programs', '-show_chapters', '-show_private_data', '-print_format', 'json', file])
			let probeData = []
			let errData = []
	
			proc.stdout.setEncoding('utf8')
			proc.stderr.setEncoding('utf8')
	
			proc.stdout.on('data', function (data) { probeData.push(data) })
			proc.stderr.on('data', function (data) { errData.push(data) })
	
			proc.on('exit', code => { exitCode = code })
			proc.on('error', err => reject(err))
			proc.on('close', () => resolve(JSON.parse(probeData.join(''))))
		})
	}

	return module.exports.SYNC ? doProbeSync : doProbe
})()
