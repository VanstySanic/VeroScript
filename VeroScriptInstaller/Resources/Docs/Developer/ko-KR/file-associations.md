# 파일 연결 구현

공개 동작은 [설치 가이드](../../User/ko-KR/index.md#file-associations)에서 관리합니다. `Core/FileAssociationModels.cs`는 `FileAssociationOptions`, `FileAssociationRecord`와 값별 소유권 기록을 선언합니다. `InstallOptions.FileAssociations`의 기본값은 모두 false입니다. `InstallerEngine.SupportsFileAssociations`와 `GetSupportedFileAssociations(manifest)`는 레지스트리 어댑터와 패키지 기능을 노출하며 설치에는 선택한 Runtime, Windows 매니페스트와 해당 실행 파일/아이콘 선언이 필요합니다.

`Core/FileAssociationRegistration.cs`가 변경을 계획하고 `Core/Platform/IFileAssociationRegistry.cs`가 계획과 레지스트리 접근을 분리합니다. Windows 구현은 현재 사용자의 Classes, 등록 앱과 소유한 기능 항목을 기록합니다. 처리기 ID는 설치별로 구분합니다. 확장자 기본 ProgID나 `UserChoice`는 수정하지 않으며 `.vspkg`에는 보호된 DefaultIcon만 등록합니다. `Core/Platform/DefaultAppsSettings.cs`의 성공은 설정 페이지를 요청했다는 의미입니다.

설치 기록의 선택적 `fileAssociations` 필드는 옵션, 소유자, 값과 생성 키를 저장하며 필드가 없으면 기록된 연결이 없습니다. 적용 전 의도를 저널에 기록하고 적용 시 이전 값과 소유자 마커를 다시 확인하며 역순으로 롤백합니다. 복구/업데이트는 이후 사용자 변경을 되찾지 않고 tombstone으로 소유권 상실을 유지합니다. 소유한 빈 키만 정리하여 다른 앱과 동시 기록을 보존합니다.

번들 Tool의 `install` 명령은 값 없는 플래그 `--associate-vs`, `--associate-vsapp`, `--package-icon`, `--choose-defaults`를 받으며 마지막 플래그는 앞의 두 개 중 하나 이상을 요구합니다. 이는 Tool 플래그이며 GUI 설치 인수가 아닙니다. App은 선택적 마법사 페이지와 `App/Resources/Localization/<locale>/associations.json`을 관리합니다. 여섯 언어의 키와 자리표시자를 맞추고 설정 열기 실패를 설치 성공과 구분합니다.

실행이 허용되면 격리된 레지스트리 어댑터/하이브로 기능 부재, Runtime 선택 해제, 기존 소유자, 사용자 변경, 복구/업데이트, 부분 적용, 복원과 제거를 검증합니다. 실제 Windows 셸 동작은 별도로 확인합니다. 정적 문서 검사로 레지스트리 효과, 아이콘 갱신이나 기본 앱 UI를 인증하지 않습니다.

레지스트리 키와 값 이름은 대소문자를 구분하지 않지만 소유자 ID 값은 정확히 일치해야 합니다. 적용과 롤백은 UI 스레드 밖에서 실행합니다. 롤백은 관리 파일보다 등록 값을 먼저 복원하고 중간 셸 알림을 억제하며 참조와 원래 아이콘 경로를 모두 사용할 수 있게 된 뒤 알립니다.

<!-- docs-navigation -->

---

[Installer](index.md)

[en-US](../en-US/file-associations.md) · [fr-FR](../fr-FR/file-associations.md) · [ja-JP](../ja-JP/file-associations.md) · [ko-KR](file-associations.md) · [zh-CN](../zh-CN/file-associations.md) · [zh-TW](../zh-TW/file-associations.md)
