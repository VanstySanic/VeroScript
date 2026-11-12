# Installer lifecycle and safety

## Components and staging

`Core/` owns manifest validation, bundle archives, package metadata, path safety,
platform information, retries, and the install engine. `App/` owns the graphical
host and localized progress. `Tool/` owns bundle/build entry points. The core is
usable without the graphical app.

Installation stages each selected component, verifies copied files, writes an
installation record, and commits the transition. The record tracks product
identity, component roots, owned files, manifest fingerprint, and runtime PATH
mutation.

Before PATH or live component directories are changed, the engine writes an
atomic transaction journal beside every affected component root and holds an
exclusive per-parent-directory transaction lock. A later install, repair,
uninstall, or `LoadRecordAsync` call automatically recovers an interrupted
prepared transaction or finishes cleanup for a durable
committed transaction. Journal copies are reconciled conservatively so a crash
while persisting the final commit marker cannot roll back a completed commit.
One deterministically selected journal copy is the durable commit authority and
is deleted last; failures during post-commit backup cleanup are retried by a
later operation and never turn a completed installation into a reported rollback.
Successful rollback is likewise recorded as a terminal state before journals are
removed, so a partially deleted journal set cannot replay PATH changes after a
new installation has started.

`LoadRecordAsync` is therefore not a read-only inspection API: it recovers
transactions discovered beside the requested component root before opening
`.veroscript-install.json`. The GUI repair/uninstall startup and Tool record
loading use this same path, so a record temporarily moved into a backup during
an interrupted operation can be restored before the UI tries to display it.
An active competing installer can cause record loading to fail with a lock
error; recovery does not bypass that lock.

Installation checks both the selected destination roots and, when supplied,
the root containing `ExistingInstallRecordPath` before loading the old record.
This also handles retrying an interrupted relocation with a different new
destination. Transaction states are `prepared`, `committed`, and `rolledback`;
terminal journals are cleanup markers and must not replay earlier PATH changes.

## Safety boundaries

Destination roots cannot overlap the protected payload or one another. Managed
destinations and ancestors are checked for links. Relative paths are resolved
under their intended root; traversal, absolute escapes, foreign entries, and
ambiguous ownership fail closed. Retried filesystem operations must not turn a
partial operation into success.

## Repair, relocation, and uninstall

Repair requires the same product version, platform, architecture, and manifest
fingerprint recorded by the installation. Relocation validates existing and
sibling records, ownership, destination safety, and staged paths before moving.
Uninstall first stages all unowned files, then removes the owned PATH entry and
commits every component as one recoverable transaction. It removes only recorded
files and owned PATH entries and preserves unowned files. Failures before the
durable commit marker trigger rollback; a failed rollback retains its journal
and backups for a subsequent recovery attempt. After durable commit, backup or
journal cleanup failures do not report the successful installation as failed.

Do not manually delete transaction journals, stage directories, or backups while
an operation is active or awaiting recovery. Reopen the same product operation
using the original record/root. If recovery still fails, retain those files and
inspect the reported filesystem/PATH error before retrying.

## Regression coverage

The test suite covers installation, update, relocation, optional-component
removal, repair and uninstall, including user-file preservation and PATH
ownership. Recovery cases include multi-parent journal reconciliation, stale
terminal journals, record loading before GUI operation startup, and relocation
retried through the old record root. Follow the
[release validation procedure](release-validation.md) to validate the candidate,
record its results, and distinguish engine tests from manual GUI testing.

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](lifecycle-and-safety.md) · [fr-FR](../fr-FR/lifecycle-and-safety.md) · [ja-JP](../ja-JP/lifecycle-and-safety.md) · [ko-KR](../ko-KR/lifecycle-and-safety.md) · [zh-CN](../zh-CN/lifecycle-and-safety.md) · [zh-TW](../zh-TW/lifecycle-and-safety.md)
