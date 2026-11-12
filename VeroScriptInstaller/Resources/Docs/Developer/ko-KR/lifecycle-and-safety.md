# Installer lifecycle과 safety

## component와 staging

`Core/`는 manifest validation, bundle archive, package metadata, path safety, platform information, retry, install engine을 소유합니다. `App/`는 graphical host/localized progress, `Tool/`는 bundle/build entry point를 소유하며 Core는 graphical app 없이 사용 가능합니다.

installation은 selected component를 stage하고 copied file을 verify한 후 installation record를 쓰고 transition을 commit합니다. record는 product identity, component root, owned file, manifest fingerprint, runtime PATH mutation을 추적합니다.

PATH/live component directory 변경 전에 engine은 affected component root마다 atomic transaction journal을 쓰고 exclusive per-parent-directory transaction lock을 잡습니다. 뒤의 install/repair/uninstall/`LoadRecordAsync`는 interrupted prepared transaction을 recover하거나 durable committed transaction cleanup을 완료합니다. journal copy는 conservative하게 reconcile되어 final commit marker persist 중 crash가 completed commit을 rollback하지 않습니다. deterministically selected copy가 durable authority이며 마지막에 delete됩니다. post-commit backup cleanup failure는 나중에 retry하고 successful installation을 rollback으로 report하지 않습니다. successful rollback도 journal remove 전 terminal state로 기록하여 partial journal delete가 new installation 뒤 PATH change를 replay하지 않습니다.

`LoadRecordAsync`는 read-only inspection API가 아니며 requested component root 옆 transaction을 recover한 후 `.veroscript-install.json`을 엽니다. GUI repair/uninstall startup과 Tool record loading은 같은 path를 써 interrupted operation 중 backup으로 옮겨진 record를 UI 표시 전에 restore할 수 있습니다. active competing installer는 lock error를 일으킬 수 있고 recovery는 lock을 bypass하지 않습니다.

installation은 selected destination root 및 제공된 `ExistingInstallRecordPath`가 든 root 모두를 old record load 전에 check합니다. different new destination으로 interrupted relocation retry를 처리합니다. state는 `prepared`, `committed`, `rolledback`이며 terminal journal은 cleanup marker로 PATH change를 replay하지 않습니다.

## 안전성 경계

destination root는 protected payload/서로 overlap하지 않습니다. managed destination/ancestor는 link를 확인합니다. relative path는 intended root 아래 resolve하고 traversal, absolute escape, foreign entry, ambiguous ownership은 fail closed입니다. retried filesystem operation은 partial operation을 successful로 만들지 않습니다.

## 복구, 이동과 제거

repair는 record의 product version/platform/architecture/manifest fingerprint을 요구합니다. relocation은 existing/sibling record, ownership, destination safety, staged path를 validate한 뒤 move합니다. uninstall은 all unowned file을 먼저 stage하고 owned PATH entry를 remove한 뒤 every component를 one recoverable transaction으로 commit합니다. recorded file/owned PATH entry만 remove하고 unowned file은 보존합니다. durable commit 전 failure는 rollback하며 failed rollback은 next recovery용 journal/backup을 남깁니다. durable commit 뒤 cleanup failure는 success를 failure로 report하지 않습니다.

active operation/recovery 대기 중 transaction journal, stage directory, backup을 수동 delete하지 마십시오. original record/root에서 같은 product operation을 reopen합니다. recovery가 다시 failure면 file을 보존하고 reported filesystem/PATH error를 inspect 후 retry합니다.

## 회귀 테스트 범위

test suite는 install, update, relocation, optional-component removal, repair, uninstall, user-file preservation, PATH ownership을 cover합니다. recovery에는 multi-parent journal reconciliation, stale terminal journal, GUI startup 전 record loading, old record root를 통한 relocation retry가 포함됩니다. [게시 검증 절차](release-validation.md)에 따라 후보를 검증하고 결과를 기록하세요. 엔진 테스트와 GUI 수동 검증을 구분합니다.

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ko-KR/index.md)

언어: [en-US](../en-US/lifecycle-and-safety.md) · [fr-FR](../fr-FR/lifecycle-and-safety.md) · [ja-JP](../ja-JP/lifecycle-and-safety.md) · [ko-KR](lifecycle-and-safety.md) · [zh-CN](../zh-CN/lifecycle-and-safety.md) · [zh-TW](../zh-TW/lifecycle-and-safety.md)
