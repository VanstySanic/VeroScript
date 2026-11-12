# 제품별 릴리스와 독립 버전

유일한 [패키징·배포 규범](../../../../../Resources/Docs/Standards/ko-KR/PACKAGING.md)을 따른다. 구형 구조 호환·변환은 제공하지 않으며 기존 결과물·불필요 후보를 정리한 뒤 재빌드한다. 제품 버전은 배포 등록 정보를 따른다.

제품 우선 `Release/<business-route>/<target>/<version>/`에서 용도로 `archives/`, `sdks/`, `packages/`, `installers/`, `symbols/`를 구분한다. 공유는 `any`, WebAssembly SDK는 `wasm32`, 네이티브는 실제 OS/아키텍처를 사용한다. `release-index.json`은 `indexes/<target>.json`을 참조하며 한 제품이 공유·네이티브 여러 매니페스트를 참조할 수 있다. 전체 계약은 규범을 기준으로 한다.

공통 등록부와 배포 스크립트는 분류 경로, 형식 검사와 검증 기록을 구현합니다. 배포에는 실제 빌드 성공과 페이로드/목록 감사 통과가 필요합니다. 대상 실행과 설치 수명 주기는 실패나 미실행을 포함해 별도 기록합니다. 순수 계획과 정적 검사만으로 빌드 또는 실행 검증을 입증할 수 없습니다.

소유권과 미사용이 확인된 결과물·재생성 가능한 임시 데이터만 정리하고 소스·자격증명·유용한 도구·디스크를 보호한다. 구형 모음을 이관하지 않으며 새 모음 갱신은 잠금·복구 커밋·참조 폐쇄·해시를 계속 검증한다. 문서만 갱신할 때는 실제 패키징하지 않는다.

```text
Release/
  release-index.json
  indexes/<target>.json
  <business-route>/<target>/<version>/
    archives/
    sdks/
    packages/
    installers/
    symbols/
    release-manifest.json
    checksums.txt
```

```text
python Resources/Tools/Packaging/veroscript_docs.py check
python Resources/Tools/Packaging/veroscript_docs.py check-collection
```

`supportedTargets`는 실제 페이로드 대상을, `consumerTargets`는 참조 가능한 배포 대상을 기록한다. wasm32 SDK는 네이티브 호스트 인덱스에서 참조할 수 있다. 공통·네이티브 조합은 실제 생태계 버전을 일치시킨다.

## 계획 및 구현 진입점

저장소 루트에서 실행합니다. `configuration`은 등록된 버전, 경로, 제품 범위와 형식 버전을 읽습니다. 계획은 동일한 경로 함수를 사용하며 빌드, 준비 또는 게시를 하지 않습니다. `--target`과 `--business`를 반복해 범위를 좁힙니다. `indexSelections`는 네이티브와 공유 매니페스트를 함께 참조할 수 있으며 선택 항목과 Wheel 태그는 실제 생성 결과로 확정해야 합니다.

소스 입력과 빌드 명령은 [빌드 및 번들](build-and-bundle.md), 최종 목록 검사와 한계는 [배포 검증](release-validation.md)을 참조하세요. 아래 명령은 메타데이터만 확인합니다. 규범만 바꿀 때는 위 문서 검사까지만 수행합니다.

```text
python Resources/Tools/Packaging/veroscript_business_release.py configuration
python Resources/Tools/Packaging/veroscript_release_plan.py --target win-x64 --business Python --business Resources
```

<a id="release-qualification"></a>

## 목록 및 검증 기록

대상 인덱스의 `complete`는 필수 제품 목록이 갖춰졌음을 나타냅니다. `qualificationStatus`는 기록된 검증을 집계하며 `qualified`에는 완전한 목록과 필수 검사 충족도 필요합니다. 실제 빌드와 무결성 검사를 통과한 결과물은 실행/장치 검증이 실패하거나 미실행이어도 배포할 수 있습니다. 대상 준비 완료를 주장하기 전에 기록을 확인하며 기록이 없으면 `not-recorded`로 취급합니다.

제품 항목은 `manifests[]`의 경로와 SHA-256에 연결된 `qualification` schemaVersion 1을 가질 수 있습니다. `--records`는 게시된 제품 ID를 `checks`가 있는 객체에 매핑하는 JSON 객체입니다. 각 검사는 실제 `status`와 비어 있지 않은 `reason`을 가지며 `required` 기본값은 true이고 `evidenceSha256`은 선택 사항입니다. 성공한 `build`와 명시적으로 필수인 `target-runtime` 검사가 필요합니다. `failed`, `not-run`도 실제 결과로 기록하며 페이로드 변경 시 이전 기록은 무효입니다. 기록 도구는 모음 잠금 아래 대상 인덱스와 루트 해시를 갱신합니다.

```text
python Resources/Tools/Packaging/veroscript_qualification.py --repo-root . --release-directory Release --target win-x64 --records .tmp/acceptance-records.json
```

## 기존 컬렉션 사전 검사

게시 전 기존 대상 인덱스의 `requiredBusinesses`는 현재 제품 레지스트리 정의와 정확히 일치해야 합니다. 집합이 바뀌었다면 `.tmp` 또는 `dist` 아래 새롭고 비어 있는 전용 출력을 선택하고 소스에서 컬렉션을 다시 구성합니다. 기본 `Release` 출력도 같은 검사를 받습니다. 계획 미리보기는 `win-x86`를 포함해 등록된 모든 데스크톱 및 모바일 대상을 다루며 `--target`으로 범위를 좁힐 수 있습니다.

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ko-KR/index.md)

[en-US](../en-US/release-layout.md) · [fr-FR](../fr-FR/release-layout.md) · [ja-JP](../ja-JP/release-layout.md) · [ko-KR](release-layout.md) · [zh-CN](../zh-CN/release-layout.md) · [zh-TW](../zh-TW/release-layout.md)
