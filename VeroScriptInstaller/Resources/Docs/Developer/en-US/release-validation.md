# Release validation procedure

This page follows the sole [packaging and publication standard](../../../../../Resources/Docs/Standards/en-US/PACKAGING.md). Do not support or convert old layouts. Remove old Release outputs and obsolete candidates, then rebuild under the new rules. Product versions are defined by the release registry.

## Validation stages

Start from the intended source/configuration and the selected target profile. Use [build and bundle](build-and-bundle.md) for producer prerequisites. Validate the candidate inventory, then run the target’s required consumers, applications and installer lifecycle checks. Cross-compilation alone is not a runtime pass. Bind results to the exact final artifact hashes after signing or other byte-changing steps.

## Inventory and current-source checks

The following examples audit an existing collection from the repository root. Replace `win-x64` with the actual consuming target. A shared `any`/`wasm32` index alone cannot establish completeness of a native or mobile release.

```text
python Resources/Tools/Packaging/verify-veroscript-release.py --release-directory Release --target win-x64 --require-complete --check-source
```

`--require-complete` checks the target’s required business set and rejects partial businesses; it does not run or certify runtime/device, graphical, installer lifecycle, signing or notarization acceptance. `--check-source` additionally compares source-owned public payloads covered by the verifier with current originals; it does not prove binaries were rebuilt from that source. Omit it only when auditing the recorded contract of an earlier valid new-format collection. `--version` requires every selected business reference to have that actual version; it is not a common collection version.

```powershell
./Resources/Tools/Packaging/verify-veroscript-release.ps1 -ReleaseDirectory Release -Target win-x64 -RequireComplete -CheckSource -RequireSignedInstallers
```

## Acceptance evidence and final review

Record `passed`, `failed`, `not-run` or `not-applicable` with reasons, source identity, tools, configuration, target/device identity and final digests. Keep cross-linking, emulator/simulator, physical devices, C# AOT/trimming, Python applications and installer install/repair/uninstall results separate. Missing environments and unsigned artifacts remain explicit gaps. The Windows wrapper can additionally enforce Authenticode with `-RequireSignedInstallers`; it does not sign installers or replace lifecycle checks.

Before distributing payloads, require a successful real build and passing payload, target-identity, dependency, manifest and hash checks. Run target-runtime and installer lifecycle acceptance where available and record failures or checks not run as such; those gaps do not exclude already-built valid artifacts from distribution. Inspect the [inventory and acceptance records](release-layout.md#release-qualification) to distinguish `complete` from `qualified`. After distribution, recheck final inventory and digests; changed inputs invalidate affected evidence. Standards-only edits run just the documentation checks below, without builds or product packaging.

```text
python Resources/Tools/Packaging/veroscript_docs.py check
python Resources/Tools/Packaging/veroscript_docs.py check-collection
```

<a id="installer-download-validation"></a>

## Static installer download validation

`veroscript_installer.py` verifies the selected download name and target architecture, reads the .NET single-file bundle and compares its embedded content parts with the assembly’s bundle SHA-256 and part count. Cross-target assembly also compares that digest with the ZIP verified during staging. Linux validation checks the single executable and its permissions inside the `.tar.gz`. macOS validation checks the app and embedded payload inside the `.dmg`. DMG inspection on Windows or Linux requires 7-Zip through `VEROSCRIPT_7ZIP` or PATH. Record target-runtime and installation lifecycle checks separately.

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](release-validation.md) · [fr-FR](../fr-FR/release-validation.md) · [ja-JP](../ja-JP/release-validation.md) · [ko-KR](../ko-KR/release-validation.md) · [zh-CN](../zh-CN/release-validation.md) · [zh-TW](../zh-TW/release-validation.md)
