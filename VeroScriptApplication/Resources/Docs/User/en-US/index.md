# Application containers and execution

Use `.vsapp` to distribute a runnable application as a standard ZIP container. It contains `veroscript-app.json`, `veroscript-app.files.json`, and normally the compiled entry `program/entry.bin`; source entries are also supported. A `.vslib` contains a compiled library. Installable `.vspkg/.zip` distributions use a separate root `veroscript-package.json` and must be installed before use.

```sh
veroscript /absolute/application.vsapp
veroscript --check /absolute/application.vsapp
veroscript-runtime /absolute/application.vsapp -- argument1
veroscript-runtime /absolute/release-directory
veroscript --application-options /absolute/host-options.json /absolute/protected.vsapp
```

The same application content supports a container, directory, release ZIP or optional PE/ELF bootstrap. An EXE is optional. Runtime and dependencies can be external or bundled independently. Only an explicitly supplied environment is used; loading does not solve dependencies, install packages, invoke installation hooks or scan PATH.

For C++ use `CreateApplicationServices()`, optionally configure a frozen environment/trust/target policy, then `FApplication::Open`, `Prepare`, or `VeroScriptSession::LoadApplication`. `VeroScriptSession::LoadFile` and `VeroScriptEngine::RunFile` recognize containers/directories. `ReadEntryPackage` inspects the image without loading native factories. Source entry compilation uses the same Core module environment; in-memory libraries and source mappings keep immutable byte owners alive.

See [C++ SDK — Integration order](../../../../../Extensions/Language/Cpp/Resources/Docs/User/en-US/index.md#explicit-vm-selection).

The C ABI offers `vs_interop_application_services_create`, `vs_interop_session_load_application`, and `vs_interop_application_read`; release opaque handles with the matching destroy function. Options JSON has absolute `cacheRoot`, `stdlibSearchPaths`, `cryptoLibrary`, `keyring`, and trust fields `requireSignature`, `minimumSignatures`, `requiredKeys`, `policyIdentity`. Existing package byte APIs accept plain application ZIPs. Language wrappers using these C APIs share the same loader rather than implementing ZIP or cryptography.

Content inventory covers every stored file, including resource/native/runtime/environment bytes, descriptor and executable modes. Extra files, traversal, case collisions, special files, invalid local ZIP headers, size/expansion limits and invalid manifests fail. Content paths are UTF-8, slash-separated, and at most 511 bytes (the archive header validation limit). Stored bytes are checked before use. Writable caches are distinct from application content, contain ownership receipts, and use cooperating same-user process leases. Corrupt owned cache content is repaired; foreign directories are refused. Publishers cannot replace a directory held by a cooperating consumer.

Optional XChaCha20-Poly1305 IETF encryption protects compiled entry images, libraries and explicitly selected resource blobs. Root ZIP and metadata remain readable; encrypted source imports and encrypted installation ZIPs are unsupported. The explicit libsodium adapter loads an absolute native library and retains its module lease. Core reads only authenticated plaintext after verification. No plaintext temporary library is created.

Ed25519 records are arrays of 1–16 entries with kind, exact byte length, SHA-256 and key ID/version bound into the signature message. Internal content signatures use `veroscript-app.files.json.sig.json`; standalone artifact records use `<filename>.sig.json`. Trusted public keys and minimum independent-key thresholds come from the consumer. Removing a signature cannot disable a consumer's required-signature policy. Key aliases do not count as independent signers. Unknown/untrusted records cannot satisfy the policy. Failed or missing required services never trigger plaintext fallback.

Platform code signing remains a platform build step. Append single-file content before Authenticode signing; use an appropriate outer platform wrapper for formats that seal directory contents. Runtime delegation happens at most once and passes explicit file configuration and script arguments, not native callbacks or secret memory. GUI, mobile lifecycle, OS store approval and platform-specific signing are not provided by this service.

`VeroScriptSession::ReloadApplication` stages script state and the content environment together and rolls both back if initialization fails. A changed native binding catalog requires `LoadApplication` and a fresh application lifetime. Release external script value roots before replacing code. Use the existing owning-thread Session contract; cancellation cannot interrupt an arbitrary blocking host callback.

Install SDK targets with `find_package(VeroScriptApplication 0.1.1 EXACT CONFIG REQUIRED)` and link `VeroScript::VeroScriptApplication`. Package-runtime and build services have separate exported CMake packages. The runtime component descriptor identifies the actual Core build, OS, CPU, environment and ABI; it is independent of package installation's component-root descriptor.

Source entries retain owned `compile` defaults (defines, function macros, safe implicit import paths and visibility flags). Use `FPreparedApplication::CompileSource` for the common compilation entry; `VeroScriptSession::LoadApplication` calls it automatically. Directory and single-file application signatures use content scope; native platform signing remains separate.

Reassembling a signed application requires an explicit signature policy for the new content.

GUI applications use this same `.vsapp` container. `gui-compile` supplies the baked GUI image and resources; GUI check/preview and ordinary CLI inspect/check/run consume the same file. Execution requires a GUI-enabled host and the explicitly selected compatible providers.

Unified automatic, explicit and disabled selection rules are documented in [Service backend selection](../../../../../VeroScript/Resources/Docs/User/en-US/introduction.md#backend-selection).

## Standalone Runtime execution backend options

Place host options before the application path, and application arguments after `--`. `--vm-backend` and `--jit-backend` choose execution services, separately from platform/graphics/GUI/image/font/text-layout providers. They accept `auto`, `none`, compatible files/directories, or installed IDs when a package environment supplies the resolver. Automatic VM selection can use the shipped product fallback; explicit disable is not overridden. The commands below check/prepare the application without calling its entry, then execute it with JIT disabled. GUI containers still require a matching GUI-enabled host and their compatible providers.

```sh
veroscript-runtime --check --vm-backend auto --jit-backend none /absolute/application.vsapp
veroscript-runtime --vm-backend auto --jit-backend none /absolute/application.vsapp -- argument1
```

Application content and cache paths must use physical filesystem entries. On macOS, the root-owned system aliases `/tmp`, `/var` and `/etc` are accepted only when they resolve to `/private/tmp`, `/private/var` and `/private/etc` respectively. Other symbolic links and special entries remain rejected; this does not permit links inside application content or caches.

<!-- docs-navigation -->

---

[User documentation](../../../../../Resources/Docs/User/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
