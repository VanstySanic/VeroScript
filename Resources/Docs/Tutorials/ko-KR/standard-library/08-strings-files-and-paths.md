# 08: 문자열, 파일, 경로

이 튜토리얼에서는 텍스트 파일을 읽고, 내용을 정리하고, 결과를 다른 파일에 쓰는 작은 작업을 완료합니다. 문자열, I/O 및 경로 모듈을 함께 사용합니다.

## 1단계: 작업 디렉터리 준비

스크립트 옆에 `input.txt`을 만듭니다.

```text
  VeroScript
  is practical
```

`clean_text.vs`을 생성합니다:

```VeroScript
import "io.vs";
import "path.vs";
import "string.vs";

int main()
{
	string input_path = "input.txt";
	string output_path = std::filesystem::join_path("output", "clean.txt");

	if (!std::filesystem::is_file(input_path))
	{
		print("missing: " + input_path);
		return 1;
	}

	std::filesystem::mkdir(std::filesystem::dirname(output_path));
	string content = std::io::read_file(input_path);
	string cleaned = std::strings::trim(content);
	std::io::write_file(output_path, cleaned + "\n");

	print("wrote " + output_path);
	return 0;
}
```

`io.vs`은 파일과 디렉터리를 처리하고, `path.vs`은 경로를 조인하고, `string.vs`는 텍스트를 정리합니다. 이러한 책임을 별도로 유지하면 직접 작성한 경로 논리와 반복되는 문자열 도우미를 피할 수 있습니다.

## 2단계: 결과 실행 및 검사

```powershell
veroscript --check clean_text.vs
veroscript clean_text.vs
Get-Content output/clean.txt
```

출력 파일의 앞뒤 ASCII 공백이 제거됩니다. `trim`은 끝의 `0x09`부터 `0x0D`까지와 공백(`0x20`) 바이트만 제거하며 내부 및 비 ASCII 바이트는 유지합니다. `is_file`은 심볼릭 링크를 따라가며 입력 경로가 일반 파일을 가리키는지 확인합니다. `read_file` 및 `write_file`은 현재 작업 디렉터리의 상대 경로를 확인하므로 터미널 명령이 실행되는 위치에 주의하세요.

## 3단계: 개별 라인 처리

각 줄을 별도의 처리가 필요한 경우 텍스트를 분할하고 결합하세요.

```VeroScript
import "io.vs";
import "string.vs";

int main()
{
	string content = std::io::read_file("input.txt");
	array<string> lines = std::strings::split(content, "\n");
	array<string> cleaned = array<string>();

	for (string line : lines)
	{
		if (std::strings::trim(line) != "")
		{
			cleaned.push(std::strings::trim(line));
		}
	}

	std::io::write_file(
		"clean-lines.txt",
		std::strings::join(cleaned, "\n") + "\n");
	return 0;
}
```

Windows 텍스트에서는 `\r\n`을 사용할 수 있습니다. 입력이 여러 플랫폼에서 올 수 있는 경우 하나의 줄 끝 규칙을 가정하는 대신 `\r`을 명시적으로 처리하십시오.

## 4단계: 결과 형식 지정

파일을 처리한 후 읽을 수 있는 요약이 유용한 경우가 많습니다. `format`은 중괄호 필드를 사용하고 문자열을 반환합니다. `print`의 다중 인수 형식은 동일한 방언을 사용합니다.

```VeroScript
import "string.vs";

int main()
{
	string path = "output/clean.txt";
	int line_count = 2;
	string summary = format("wrote {}, lines={}", path, line_count);
	print(summary);
	printf("status=%s%%\n", "100");
	return 0;
}
```

printf 스타일 변환이 필요한 경우 `formatf` 또는 `printf`을 사용하십시오. `%d` 또는 `%.2f` 구문을 `format` 문자열에 넣지 마십시오. 필드, 너비, 정밀도 및 변환은 [문자열 언어 동작](../../../../../VeroScript/Resources/Docs/User/ko-KR/language-basics/strings.md#%EC%84%9C%EC%8B%9D-%EC%A7%80%EC%A0%95)을 참조하세요.

## 연습

1. `std::filesystem::change_extension`을 사용하여 출력 확장자를 `.clean.txt`으로 변경합니다.
2. 입력 파일의 `std::filesystem::basename`, `dirname` 및 `extension`를 인쇄합니다.
3. 출력이 존재하는 경우 `std::io::append_file`를 사용하여 처리 레코드를 추가합니다.
4. `format`을 사용하여 요약을 생성한 다음 `formatf`을 사용하여 소수점 이하 두 자리까지 기간을 인쇄해 보십시오.
5. `output/clean.txt`을 삭제하고 `std::filesystem::is_directory`로 `output`을 확인한 다음 `std::filesystem::remove_dir`로 빈 디렉터리를 제거합니다.

[`examples/path_helpers.vs`](../../../../../VeroScript/Resources/Scripts/examples/path_helpers.vs)을 비교하세요. 전체 API는 [I/O](../../../../../VeroScript/Resources/Docs/User/ko-KR/standard-library/io.md), [경로](../../../../../VeroScript/Resources/Docs/User/ko-KR/standard-library/path.md) 및 [문자열](../../../../../VeroScript/Resources/Docs/User/ko-KR/standard-library/string.md)에 있습니다.

다음: [동시성 및 이벤트](09-concurrency-and-events.md).

<!-- docs-navigation -->

---

[튜토리얼](../index.md)

언어: [en-US](../../en-US/standard-library/08-strings-files-and-paths.md) · [fr-FR](../../fr-FR/standard-library/08-strings-files-and-paths.md) · [ja-JP](../../ja-JP/standard-library/08-strings-files-and-paths.md) · [ko-KR](08-strings-files-and-paths.md) · [zh-CN](../../zh-CN/standard-library/08-strings-files-and-paths.md) · [zh-TW](../../zh-TW/standard-library/08-strings-files-and-paths.md)
