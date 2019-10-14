const ffprobe = module.exports = require('./lib/ffprobe.js')

if (require.main === module) {
	const exit = function (code, msg) {
		process.nextTick(() => process.exit(code))

		if (code !== 0) console.error(msg)
		else console.log(msg)
	}

	const args = process.argv.slice(2)

	if (args.length === 0)
		return exit(1, "Usage: node index.js /path/to/audio/file.mp3");

	!async function probeFile(file) {
		if (!file) return exit(0, 'Finished probing all files')
		try {
			const results = await ffprobe(file)
			
			console.log('%s\n========================================\n%s\n\n', file, JSON.stringify(results, null, '   '))
	
			probeFile(args.shift())
		} catch(err) {
			console.log(err)
		}
	}(args.shift())
}