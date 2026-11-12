# 설치 관리자 자동화

유지보수자 및 배포 도구 작성자를 위한 문서입니다. Tool 명령은 소스 체크아웃과 .NET 9 SDK에서 실행하세요. 설치 번들은 포터블 Runtime 아카이브와 다릅니다. GUI 작업은 [설치 가이드](../../User/ko-KR/index.md)를 참조하세요.

설치 위치를 바꾸기 위해 파일을 직접 이동하지 마십시오. 안전한 이전이나 업데이트에는 Tool에 기존 설치 레코드를 명시합니다.

이 예제는 검증, 최초 설치, 이전, 복구 및 제거의 전체 수명 주기를 보여 줍니다. 필요한 단계만 실행하세요. `runtime`만 선택하므로 `--source-runtime`만으로 준비한 번들에서도 사용할 수 있습니다. 매니페스트에 해당 구성 요소가 있을 때만 `--components`에 `examples`를 추가하세요. 업데이트 시 생략한 선택 구성 요소는 제거됩니다. `--root`는 제품 루트이며 Runtime 구성 요소는 그 아래 `Runtime` 디렉터리에 설치됩니다.

```powershell
$tool = '.\VeroScriptInstaller\Tool\VeroScriptInstaller.Tool.csproj'
$bundle = 'D:\Bundles\runtime-installer-bundle'
dotnet run --project $tool -c Release -- verify --bundle $bundle
dotnet run --project $tool -c Release -- install --bundle $bundle --root 'D:\Apps\VeroScript' --components runtime
dotnet run --project $tool -c Release -- install --bundle $bundle --root 'D:\Apps\VeroScriptMoved' --existing-record 'D:\Apps\VeroScript\Runtime\0.1.1\.veroscript-install.json' --components runtime
dotnet run --project $tool -c Release -- repair --bundle $bundle --record 'D:\Apps\VeroScriptMoved\Runtime\0.1.1\.veroscript-install.json'
dotnet run --project $tool -c Release -- uninstall --record 'D:\Apps\VeroScriptMoved\Runtime\0.1.1\.veroscript-install.json'
```

Tool의 설치 관리자 번들은 `manifest.json`과 `payload/`를 포함하며 일반 Runtime ZIP이 아닙니다. `--add-to-path`는 값을 취하지 않는 명시적 스위치입니다. 업데이트/이전 중 이를 생략하면 이전 설치가 소유하던 PATH 항목이 제거됩니다. 다운그레이드는 기본적으로 거부되며 필요한 경우에만 `--allow-downgrade`를 사용하십시오.

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ko-KR/index.md)

언어: [en-US](../en-US/automation.md) · [fr-FR](../fr-FR/automation.md) · [ja-JP](../ja-JP/automation.md) · [ko-KR](automation.md) · [zh-CN](../zh-CN/automation.md) · [zh-TW](../zh-TW/automation.md)
