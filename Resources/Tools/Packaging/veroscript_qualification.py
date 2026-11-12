# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Record target acceptance independently from payload generation and integrity."""
import argparse
import json
import os
from pathlib import Path
import re
import tempfile
import uuid

STATUSES = {'passed', 'failed', 'not-run', 'not-applicable'}


def bind_qualification(record, references):
	if record is None:
		return None
	if not isinstance(record, dict) or set(record) != {'checks'} or not isinstance(record['checks'], dict) or not record['checks']:
		raise ValueError('Qualification requires explicit checks')
	checks = {}
	for name, check in record['checks'].items():
		if (not isinstance(name, str) or not re.fullmatch('[a-z][a-z0-9-]*', name) or not isinstance(check, dict)
			or set(check) - {'status', 'reason', 'required', 'evidenceSha256'}
			or check.get('status') not in STATUSES or type(check.get('required', True)) is not bool
			or not isinstance(check.get('reason'), str) or not check['reason'].strip()):
			raise ValueError('Invalid target qualification check: ' + str(name))
		if 'evidenceSha256' in check and (not isinstance(check['evidenceSha256'], str) or not re.fullmatch('[0-9a-f]{64}', check['evidenceSha256'])):
			raise ValueError('Invalid qualification evidence digest')
		checks[name] = {**check, 'required': check.get('required', True)}
	if checks.get('build', {}).get('status') != 'passed':
		raise ValueError('A distributed payload must have a successful real build')
	if 'target-runtime' not in checks or not checks['target-runtime']['required']:
		raise ValueError('Qualification must explicitly record required target-runtime acceptance')
	checks['inventory'] = {'status': 'passed', 'required': True, 'reason': 'Published payload inventory, hashes and declared target identity verified'}
	required = [check['status'] for check in checks.values() if check['required']]
	status = 'failed' if 'failed' in required else 'not-run' if 'not-run' in required else 'passed'
	return {'schemaVersion': 1, 'status': status, 'checks': checks,
		'manifests': sorted(({'manifest': ref['manifest'], 'sha256': ref['sha256']} for ref in references), key=lambda ref: ref['manifest'])}


def verify_qualification(record, references):
	if record is None:
		return 'not-recorded'
	if not isinstance(record, dict) or record.get('schemaVersion') != 1 or set(record) != {'schemaVersion', 'status', 'checks', 'manifests'}:
		raise ValueError('Invalid target qualification record')
	if bind_qualification({'checks': record['checks']}, references) != record:
		raise ValueError('Qualification is stale or differs from its payload manifest references')
	return record['status']


def target_qualification(businesses, complete):
	statuses = [verify_qualification(entry.get('qualification'), entry['manifests']) for entry in businesses.values()]
	status = ('failed' if 'failed' in statuses else 'not-recorded' if 'not-recorded' in statuses
		else 'not-run' if 'not-run' in statuses else 'passed' if statuses else 'not-recorded')
	return {'qualificationStatus': status, 'qualified': bool(complete and statuses and status == 'passed')}


def record_qualifications(repo, root, target, records):
	from veroscript_business_release import collection_root, acquire_collection_lock, release_collection_lock, _read_indexes, _index_state, manifest_references
	from veroscript_release import digest, scratch_directory
	root = collection_root(repo, root);lock = acquire_collection_lock(repo, root)
	try:
		metadata, indexes = _read_indexes(root)
		if target not in indexes or not set(records).issubset(indexes[target]['businesses']):
			raise ValueError('Qualification selects absent target/business payloads')
		index = indexes[target]
		manifest_references(root, index)
		for business, record in records.items():
			entry = index['businesses'][business]
			entry['qualification'] = bind_qualification(record, entry['manifests'])
		index = _index_state(root, target, index['businesses'])
		index_file = root / 'indexes' / (target + '.json');root_file = root / 'release-index.json'
		before = {index_file: index_file.read_bytes(), root_file: root_file.read_bytes()}
		with tempfile.TemporaryDirectory(prefix='veroscript-qualification-', dir=scratch_directory(repo)) as temporary:
			stage = Path(temporary)
			try:
				prepared = stage / 'index.json';prepared.write_text(json.dumps(index, indent=2)+'\n', encoding='utf-8');os.replace(prepared, index_file)
				metadata['generation'] = uuid.uuid4().hex;metadata['indexes'][target]['sha256'] = digest(index_file)
				prepared = stage / 'root.json';prepared.write_text(json.dumps(metadata, indent=2)+'\n', encoding='utf-8');os.replace(prepared, root_file)
			except BaseException:
				for i, (path, data) in enumerate(before.items()):
					prepared=stage/f'restore-{i}';prepared.write_bytes(data);os.replace(prepared,path)
				raise
	finally:
		release_collection_lock(lock)


if __name__ == '__main__':
	parser=argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--repo-root',type=Path,required=True);parser.add_argument('--release-directory',type=Path,required=True)
	parser.add_argument('--target',required=True);parser.add_argument('--records',type=Path,required=True)
	args=parser.parse_args();record_qualifications(args.repo_root.resolve(),args.release_directory.resolve(),args.target,json.loads(args.records.read_text(encoding='utf-8-sig')))
