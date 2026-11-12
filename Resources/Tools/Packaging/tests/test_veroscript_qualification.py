# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

import copy
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from veroscript_qualification import bind_qualification, verify_qualification, target_qualification


class QualificationTests(unittest.TestCase):
	def setUp(self):
		self.refs=[{'manifest':'Runtime/linux-arm64/0.1.1/release-manifest.json','sha256':'a'*64}]
		self.record={'checks':{'build':{'status':'passed','reason':'Real compiler/linker outputs validated'},'target-runtime':{'status':'not-run','reason':'No native ARM64 device'}}}

	def test_unaccepted_payload_can_be_distributed_without_becoming_qualified(self):
		value=bind_qualification(self.record,self.refs)
		self.assertEqual(value['status'],'not-run')
		self.assertEqual(verify_qualification(value,self.refs),'not-run')
		self.assertEqual(target_qualification({'Runtime':{'manifests':self.refs,'qualification':value}},True),{'qualificationStatus':'not-run','qualified':False})

	def test_failed_runtime_is_preserved(self):
		self.record['checks']['target-runtime']['status']='failed'
		self.assertEqual(bind_qualification(self.record,self.refs)['status'],'failed')

	def test_missing_acceptance_is_not_inferred_as_passed(self):
		self.assertEqual(verify_qualification(None,self.refs),'not-recorded')
		self.record['checks'].pop('target-runtime')
		with self.assertRaises(ValueError):
			bind_qualification(self.record,self.refs)

	def test_failed_or_missing_real_build_is_rejected(self):
		for status in ('failed','not-run','not-applicable'):
			self.record['checks']['build']['status']=status
			with self.assertRaises(ValueError):
				bind_qualification(self.record,self.refs)

	def test_different_artifact_references_invalidate_acceptance(self):
		value=bind_qualification(self.record,self.refs)
		changed=copy.deepcopy(self.refs)
		changed[0]['sha256']='b'*64
		with self.assertRaises(ValueError):
			verify_qualification(value,changed)

	def test_inventory_and_rollup_cannot_be_forged(self):
		value=bind_qualification(self.record,self.refs)
		value['status']='passed'
		with self.assertRaises(ValueError):
			verify_qualification(value,self.refs)

	def test_complete_inventory_is_required_for_qualified_target(self):
		self.record['checks']['target-runtime']['status']='passed'
		value=bind_qualification(self.record,self.refs)
		entries={'Runtime':{'manifests':self.refs,'qualification':value}}
		self.assertFalse(target_qualification(entries,False)['qualified'])
		self.assertTrue(target_qualification(entries,True)['qualified'])

	def test_invalid_evidence_and_check_types_raise_validation_errors(self):
		for evidence in (None,True,'short'):
			self.record['checks']['build']['evidenceSha256']=evidence
			with self.assertRaises(ValueError):
				bind_qualification(self.record,self.refs)


if __name__=='__main__':
	unittest.main()
