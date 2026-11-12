# 게시 검증 절차

유일한 [패키징·배포 규범](../../../../../Resources/Docs/Standards/ko-KR/PACKAGING.md)을 따른다. 구형 구조 호환·변환은 제공하지 않으며 기존 결과물·불필요 후보를 정리한 뒤 재빌드한다. 제품 버전은 배포 등록 정보를 따른다.

## 검증 단계

의도한 소스·구성과 대상 프로필을 확정하고 생성기 전제 조건은 [빌드 및 번들](build-and-bundle.md)에서 확인합니다. 후보 목록을 검사한 다음 대상에서 필수 소비자, 앱과 설치 수명 주기를 실행합니다. 교차 컴파일만으로 실행 통과가 되지 않습니다. 서명 등 바이트를 바꾸는 단계 이후의 최종 해시에 결과를 연결합니다.

## 목록 및 현재 소스 검사

다음은 저장소 루트에서 기존 모음을 감사하는 예입니다. `win-x64`를 실제 소비 대상에 맞추세요. 공유 `any`/`wasm32` 인덱스만으로 네이티브 또는 모바일 전체 배포를 입증할 수 없습니다.

```text
python Resources/Tools/Packaging/verify-veroscript-release.py --release-directory Release --target win-x64 --require-complete --check-source
```

`--require-complete`는 대상의 필수 제품 집합을 확인하고 부분 제품을 거부합니다. 실행/장치, 그래픽, 설치 수명 주기, 서명이나 공증 검증을 실행하거나 인증하지 않습니다. `--check-source`는 검사기가 다루는 소스 소유 공개 페이로드를 현재 원본과 추가 비교하며 해당 소스에서 바이너리를 다시 빌드했다는 증거가 아닙니다. 이전의 유효한 새 형식 모음에 기록된 계약만 감사할 때 생략합니다. `--version`은 선택한 모든 제품 참조에 해당 실제 버전을 요구하며 모음 공통 버전이 아닙니다.

```powershell
./Resources/Tools/Packaging/verify-veroscript-release.ps1 -ReleaseDirectory Release -Target win-x64 -RequireComplete -CheckSource -RequireSignedInstallers
```

## 검증 증거 및 최종 확인

`passed`, `failed`, `not-run`, `not-applicable`을 이유, 소스 식별자, 도구, 구성, 대상/장치 식별자 및 최종 해시와 함께 기록합니다. 교차 링크, 에뮬레이터/시뮬레이터, 실기기, C# AOT/트리밍, Python 앱과 설치/복구/제거를 구분합니다. 환경 부재와 미서명은 명시적 부족 사항으로 남깁니다. Windows 래퍼의 `-RequireSignedInstallers`는 Authenticode를 요구할 수 있지만 서명하거나 수명 주기 검증을 대신하지 않습니다.

배포 전 실제 빌드 성공 및 페이로드, 대상 식별, 의존성, 매니페스트와 해시 검사 통과가 필요합니다. 환경이 있으면 대상 실행과 설치 수명 주기를 검증하고 실패나 미실행을 그대로 기록합니다. 이러한 부족 사항만으로 이미 빌드된 유효한 결과물을 배포에서 제외하지 않습니다. [목록 및 검증 기록](release-layout.md#release-qualification)에서 `complete`와 `qualified`를 구분하세요. 배포 후 최종 목록과 해시를 다시 확인하며 입력이 바뀌면 관련 증거를 무효화합니다. 규범만 바꿀 때는 아래 문서 검사만 수행하고 빌드나 제품 패키징은 실행하지 않습니다.

```text
python Resources/Tools/Packaging/veroscript_docs.py check
python Resources/Tools/Packaging/veroscript_docs.py check-collection
```

<a id="installer-download-validation"></a>

## 설치 관리자 다운로드 정적 검증

`veroscript_installer.py`는 다운로드 이름과 대상 아키텍처를 확인하고 .NET 단일 파일 번들을 읽어 내장 조각을 어셈블리에 기록된 번들 SHA-256 및 조각 수와 비교합니다. 교차 대상 조립에서는 준비 단계에서 검증한 ZIP의 다이제스트와도 비교합니다. Linux는 `.tar.gz` 안의 단일 실행 파일과 실행 권한을, macOS는 `.dmg` 안의 앱과 내장 페이로드를 확인합니다. 다른 운영체제에서 DMG를 검사하려면 `VEROSCRIPT_7ZIP` 또는 PATH의 7-Zip이 필요합니다. 대상 실행 및 설치 수명 주기 검사는 별도로 기록합니다.

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ko-KR/index.md)

언어: [en-US](../en-US/release-validation.md) · [fr-FR](../fr-FR/release-validation.md) · [ja-JP](../ja-JP/release-validation.md) · [ko-KR](release-validation.md) · [zh-CN](../zh-CN/release-validation.md) · [zh-TW](../zh-TW/release-validation.md)
