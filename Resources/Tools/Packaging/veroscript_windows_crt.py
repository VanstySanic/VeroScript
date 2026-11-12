# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Select native CRT DLLs from the chosen Microsoft redistributable component."""
from veroscript_release import TARGETS,binary_prefix,native_identity


def target_crt_files(directory,target):
	wanted=TARGETS[target]
	result=[]
	for path in sorted(directory.glob('*.dll')):
		actual=native_identity(binary_prefix(path))
		# Microsoft's ARM64 component also carries this x64 exception runtime for
		# emulated x64 applications. Native ARM64 imports VCRUNTIME140 instead.
		if target=='win-arm64' and path.name.lower()=='vcruntime140_1.dll' and actual==TARGETS['win-x64']:
			continue
		if actual!=wanted:
			raise ValueError('Redistributable CRT architecture mismatch: '+str(path))
		result.append(path)
	if not {'msvcp140.dll','vcruntime140.dll'}.issubset({p.name.lower() for p in result}):
		raise ValueError('Incomplete target Microsoft CRT component')
	return result
