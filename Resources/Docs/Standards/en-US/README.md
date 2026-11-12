# VeroScript project development and contribution standards

These standards are publicly readable and **apply only to development of and contributions to the VeroScript project**. They are not a language specification, SDK usage requirement, or additional condition of the MIT License. Using, integrating, or modifying VeroScript in another project does not require adopting these coding conventions or naming this project’s holder as the copyright holder of your own code. Existing copyright and license notices remain subject to the [MIT License](../../../../LICENSE) and applicable third-party licenses.

Standards remain in the source repository and are included as developer material in the complete `All` documentation. The `User` documentation, user documentation in Resources, Runtime/SDK/extension archives, and installers omit Standards.

These documents contain the **manually maintained coding and publication standards** for this project. Coding rules cover repository-owned, manually maintained source code, including the VeroScript core, tools, language extensions, and other native and application modules. [PACKAGING.md](PACKAGING.md) covers official release collections and their acceptance.

The sole maintenance source is `Resources/Docs/Standards/`. `zh-CN` is the authoritative language for defining rules; the other languages are equivalent translations and must not add or change rules. Rules follow explicit decisions by the project maintainer, rather than assumptions drawn from existing code, third-party conventions, or tool defaults. Previous rules remain in version control for comparison; only the current maintainer-approved standards apply to new or substantially modified code.

The standards are still being developed. Follow confirmed rules directly, but do not turn unspecified topics into mandatory requirements. Add new rules to the file closest to their language or subject.

## Requirement levels

- **Required**: new and substantially modified code must comply.
- **Recommended**: adopt by default; departures for language constraints, compatibility, or readability must remain locally consistent and explain the reason.
- **As needed**: use only when it improves structural clarity, maintainability, or caller understanding; do not create mechanical, repetitive boilerplate.
- **Provisional**: the current recommended default; the maintainer may supplement or replace it.

## Contents

| File | Subject | Status |
| --- | --- | --- |
| [GENERAL.md](GENERAL.md) | Shared structure, comments, line breaks, and change principles | Established |
| [CPP.md](CPP.md) | C++ formatting, type layout, declaration/definition order, macros, and switch | Confirmed |
| [CSHARP.md](CSHARP.md) | C# adaptation of C++ structural principles and provisional formatting | Provisional, reviewed |
| [PYTHON.md](PYTHON.md) | Python module, type, and comment layout | Provisional, reviewed |
| [WEB.md](WEB.md) | TypeScript/JavaScript module and type layout | Provisional, reviewed |
| [VEROSCRIPT.md](VEROSCRIPT.md) | VeroScript source and type layout | Provisional, reviewed |
| [PACKAGING.md](PACKAGING.md) | Packaging, shared artifacts, target profiles, acceptance and clean rebuilding | Adopted; layout and static contracts implemented |

## Scope and migration

- Coding rules apply to repository-owned, manually maintained source code and source comments. Packaging and publication follow [PACKAGING.md](PACKAGING.md).
- `ThirdParty/`, generated directories/files, and raw input fixtures used to test parsers are exempt from mandatory migration.
- New files must fully comply with the corresponding standard. When editing existing files, prioritize new and related code; do not broadly reformat unrelated code during a functional change.
- Valid language, ABI, protocol, and build contracts take precedence over formatting rules. Preserve the contract and record the exception in the appropriate standard.

## Maintenance

Keep rule changes small and explicit: state the rule, scope, necessary examples, and exceptions. Do not promote an incidental style in one historical file into a global requirement.

- Edit the `zh-CN` original first and update the other five languages in the same change. Requirement levels, scope, exceptions, and provisional status must remain equivalent.
- Maintain each language's text here. Review examples when rules change; do not infer new rules from existing code.
- Maintain standards in `Resources/Docs/Standards/<locale>`: `zh-CN` defines the rules and the other five languages remain equivalent. After changing standards, run `python Resources/Tools/Packaging/veroscript_docs.py check` and `check-collection`. Canonical standards are included only in `All` as developer material, not in `User` or functional releases.
- Do not translate code, APIs, protocol fields, commands, or the two-line SPDX header. Comments in code examples remain in English.
