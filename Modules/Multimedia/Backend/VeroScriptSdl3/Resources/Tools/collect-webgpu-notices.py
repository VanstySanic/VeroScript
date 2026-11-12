# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

from pathlib import Path
import argparse
import concurrent.futures
import hashlib
import io
import json
import tarfile
import time
import tomllib
import urllib.parse
import urllib.request

def main():
	parser = argparse.ArgumentParser(description='Regenerate pinned wgpu-native notices (Python 3.11+).')
	parser.add_argument('--cache', type=Path, required=True)
	args = parser.parse_args()
	module = Path(__file__).resolve().parents[2]
	manifest = json.loads((module/'ThirdParty/WgpuNative.version.json').read_text(encoding='utf-8'))
	tag = manifest['tag']
	cache = args.cache.resolve()
	cache.mkdir(parents=True, exist_ok=True)

	def download(url):
		cached = cache/('source-'+hashlib.sha256(url.encode()).hexdigest())
		if cached.exists():
			return cached.read_bytes()
		for attempt in range(4):
			try:
				with urllib.request.urlopen(url, timeout=45) as response:
					data=response.read()
					cached.write_bytes(data)
					return data
			except Exception:
				if attempt == 3:
					raise
				time.sleep(attempt+1)

	packages = tomllib.loads(download(f'https://raw.githubusercontent.com/gfx-rs/wgpu-native/{tag}/Cargo.lock').decode())['package']


	def collect_crate(package):
		name, version = package['name'], package['version']
		archive = cache/f'{name}-{version}.crate'
		if not archive.exists():
			archive.write_bytes(download(f'https://static.crates.io/crates/{name}/{name}-{version}.crate'))
		data = archive.read_bytes()
		if hashlib.sha256(data).hexdigest() != package['checksum']:
			raise ValueError(f'checksum: {name}')
		texts = []
		with tarfile.open(fileobj=io.BytesIO(data), mode='r:gz') as source:
			metadata = tomllib.loads(source.extractfile(f'{name}-{version}/Cargo.toml').read().decode())['package']
			for member in source.getmembers():
				p = Path(member.name)
				if member.isfile() and (p.name.upper().startswith(('LICENSE','COPYING','NOTICE','UNLICENSE','COPYRIGHT')) or 'LICENSES' in [v.upper() for v in p.parts]):
					value = source.extractfile(member).read()
					if len(value) < 200000:
						texts.append((member.name,value.decode('utf-8',errors='replace')))
		return name,version,metadata,texts

	selected = [p for p in packages if p.get('source','').startswith('registry+')]
	with concurrent.futures.ThreadPoolExecutor(max_workers=5) as pool:
		results = list(pool.map(collect_crate,selected))
	missing = []
	output = [f'wgpu-native {tag} and Cargo.lock dependency notices\n',
			  f'Source: https://github.com/gfx-rs/wgpu-native/tree/{tag}\n',
			  'The dependency list conservatively includes all platforms and build dependencies in the pinned lock file.\n']
	for filename in ('LICENSE.MIT','LICENSE.APACHE'):
		text = download(f'https://raw.githubusercontent.com/gfx-rs/wgpu-native/{tag}/'+filename).decode()
		output.append(f'\n===== wgpu-native / {filename} =====\n{text}\n')
	for name,version,metadata,texts in sorted(results):
		output.append(f'\n===== {name} {version} =====\nSPDX: {metadata.get("license", "see license files")}\nRepository: {metadata.get("repository", "https://crates.io/crates/"+name)}\n')
		if not texts:
			missing.append({'name':name,'version':version,'metadata':metadata})
		for path,text in texts:
			output.append(f'\n--- {path} ---\n{text}\n')

	groups={}
	for package in missing:
		name,version=package['name'],package['version']
		with tarfile.open(cache/f'{name}-{version}.crate') as source:
			vcs=json.load(source.extractfile(f'{name}-{version}/.cargo_vcs_info.json'))
		repository=package['metadata']['repository'].rstrip('/').removesuffix('.git')
		key=(repository,vcs['git']['sha1'])
		groups.setdefault(key,[]).append(package)

	def collect_workspace(item):
		(repo,sha),packages=item
		github=repo.removeprefix('https://github.com/')
		tree=json.loads(download(f'https://api.github.com/repos/{github}/git/trees/{sha}?recursive=1'))
		candidates=[v['path'] for v in tree['tree'] if v['type']=='blob' and Path(v['path']).name.upper().startswith(('LICENSE','COPYING','NOTICE','UNLICENSE'))]
		# Workspace-level license files omitted by the crate archive are the authoritative fallback.
		roots=[v for v in candidates if '/' not in v]
		if not roots:
			roots=[v for v in candidates if len(Path(v).parts)<=2]
		if not roots:
			roots=['README.md']
		texts=[]
		for path in roots:
			url=f'https://raw.githubusercontent.com/{github}/{sha}/{urllib.parse.quote(path)}'
			texts.append((url,download(url).decode('utf-8')))
		if packages[0]['metadata']['license']=='CC0-1.0':
			url='https://raw.githubusercontent.com/spdx/license-list-data/v3.27.0/text/CC0-1.0.txt'
			texts.append((url,download(url).decode('utf-8')))
		return packages,texts

	with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
		results=list(pool.map(collect_workspace,groups.items()))
	output.append('\n===== Workspace licenses omitted from crate archives =====\n')
	for packages,notices in results:
		output.append('\nApplies to: '+', '.join(p['name']+' '+p['version'] for p in packages)+'\n')
		for url,notice in notices:
			output.append('\nSource: '+url+'\n'+notice+'\n')

	headers_commit = '673658bc2bd70ec39fc55ebe6bb0173cf6d0a603'
	url = f'https://raw.githubusercontent.com/webgpu-native/webgpu-headers/{headers_commit}/LICENSE'
	output.append('\n===== webgpu-headers / LICENSE =====\nSource: '+url+'\n'+download(url).decode()+'\n')
	destination=module/'Resources/Licenses/WGPU-NOTICES.txt'
	destination.write_text(''.join(output),encoding='utf-8',newline='\n')
	print(f'{len(selected)} pinned registry dependencies; completed workspace licenses for {len(missing)} packages')


if __name__ == "__main__":
	main()
