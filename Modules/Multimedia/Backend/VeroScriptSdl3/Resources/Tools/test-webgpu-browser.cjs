// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Usage: node test-webgpu-browser.cjs --directory <build/bin> [--playwright <module>] [--browser <executable>]
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const args = process.argv.slice(2);
const options = {};
for (let i = 0; i < args.length; i += 2) {
	if (!['--directory', '--playwright', '--browser'].includes(args[i]) || !args[i + 1]) {
		throw new Error('Expected --directory <build/bin> [--playwright <module>] [--browser <executable>]');
	}
	options[args[i].slice(2)] = args[i + 1];
}
if (!options.directory)
{
	throw new Error('--directory is required');
}
const root = path.resolve(options.directory);
const {chromium} = require(options.playwright || 'playwright');
(async () => {
	const server = http.createServer((req, res) => {
		const resource = new URL(req.url, 'http://localhost').pathname;
		if (resource === '/favicon.ico') { res.writeHead(204); res.end(); return; }
		const file = path.resolve(root, '.' + resource);
		if (!file.startsWith(root + path.sep)) { res.writeHead(404); res.end(); return; }
		res.setHeader('content-type', file.endsWith('.wasm') ? 'application/wasm' :
			file.endsWith('.js') ? 'text/javascript' : 'text/html');
		fs.createReadStream(file).on('error', () => { res.statusCode = 404; res.end(); }).pipe(res);
	});
	let browser;
	try {
		await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
		browser = await chromium.launch({executablePath: options.browser, headless: true,
			args: ['--enable-unsafe-webgpu']});
		const page = await browser.newPage();
		const errors = [];
		page.on('pageerror', error => errors.push(String(error)));
		page.on('console', message => console.log(message.type() + ': ' + message.text()));
		await page.goto(`http://127.0.0.1:${server.address().port}/VeroScriptVisualWebGpuBrowser.html`);
		await page.waitForFunction(() => document.body.dataset.result, {timeout: 45000});
		const result = await page.evaluate(() => document.body.dataset.result);
		console.log('WebGPU browser result:', result);
		if (result !== 'passed' || errors.length)
		{
			throw new Error(errors.join('\n') || result);
		}
	}
	catch (error)
	{
		console.error(error);
		process.exitCode = 1;
	}
	finally
	{
		if (browser)
		{
			await browser.close();
		}
		server.close();
	}
})();
