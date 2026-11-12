# 경로 모듈

모듈 파일: `path.vs`

다음을 사용하여 가져옵니다.

```VeroScript
import "path.vs";
```

## 기능

| 기능 | 설명 |
| --- | --- |
| `std::filesystem::basename(string path)` | 최종 경로 구분 기호 뒤에 이름을 반환합니다. |
| `std::filesystem::dirname(string path)` | 최종 구분 기호 앞의 디렉터리를 반환합니다. 구분 기호가 없으면 `"."`를 반환합니다. |
| `std::filesystem::join_path(string left, string right)` | 구분 기호를 사용하여 두 경로 구성 요소를 결합합니다. |
| `std::filesystem::extension(string path)` | 점이 포함된 확장자를 반환하거나, 점이 없는 경우 빈 문자열을 반환합니다. |
| `std::filesystem::change_extension(string path, string new_ext)` | 확장자를 대체하거나 추가합니다. |

경로 모듈은 `/` 및 `\`을 모두 구분 기호로 인식합니다. `std::filesystem::join_path`는 왼쪽 구성 요소 끝에 있는 기존 구분 기호와 양쪽에 있는 빈 구성 요소를 처리합니다.

`std::filesystem::change_extension`은 앞에 점이 있거나 없는 새 확장을 허용합니다. `"json"` 및 `".json"`는 동일한 결과를 생성합니다. 파일 이름 앞의 점 자체는 확장자로 간주되지 않습니다.
`std::filesystem::dirname`은 Windows 드라이브 루트의 구분 기호를 유지합니다. 예를 들어 `std::filesystem::dirname("C:\\foo.txt")`는 `C:\`를 반환합니다.
`std::filesystem::join_path`는 문자열만 결합합니다. 한쪽이 비어 있으면 다른 쪽을 그대로 반환합니다. 오른쪽이 `/` 또는 `\\`로 시작하거나 `C:` 같은 ASCII 드라이브 접두사가 있으면 오른쪽을 반환합니다. 그 외에는 필요할 때 `/`를 넣지만 왼쪽이 정확히 `C:`이면 `C:file`이 됩니다. `.`과 `..`을 정규화하거나 파일 시스템을 조회하지 않습니다.


- [표준 라이브러리 기반](foundation.md)

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/standard-library/path.md) · [fr-FR](../../fr-FR/standard-library/path.md) · [ja-JP](../../ja-JP/standard-library/path.md) · [ko-KR](path.md) · [zh-CN](../../zh-CN/standard-library/path.md) · [zh-TW](../../zh-TW/standard-library/path.md)
