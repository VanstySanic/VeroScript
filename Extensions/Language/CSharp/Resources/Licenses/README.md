# C# tool dependency notices

The C# frontend, binding generator and transpiler distribute Roslyn assemblies.
Their build and publish outputs retain the notices in a `licenses` directory.
The frontend NuGet and both .NET tool packages carry these notices as well.
They supplement the VeroScript MIT notice and retain the upstream terms.

- `Roslyn-LICENSE.txt`: [Roslyn license at the 4.13.0 NuGet source commit](https://github.com/dotnet/roslyn/blob/75e79dace86b274327a1afe479228d82a06051a4/License.txt).
- `Roslyn-ThirdPartyNotices.rtf`: the original `ThirdPartyNotices.rtf` from
  `Microsoft.CodeAnalysis.Common` 4.13.0, identical to the CSharp package copy.

Update these notices when changing the Roslyn dependency version. Keep the
full RTF file, which can be read with an RTF-capable editor. Package consumption
tests verify both .NET tool frameworks retain the exact source notices.
