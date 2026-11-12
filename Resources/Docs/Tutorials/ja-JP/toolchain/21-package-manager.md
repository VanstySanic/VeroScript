# 21: ローカルパッケージを導入する

管理機能を有効にしたデスクトップ Runtime と、空の演習フォルダーを使います。通信やレジストリは不要です。

## 1. ファイルを作る

library/veroscript-package.json にパッケージ、library/scripts/math.vs にモジュール、app/veroscript-project.json にプロジェクト、app/main.vs に入口を保存します。archive 依存が参照する相対配置を保ってください。

### library/veroscript-package.json

```json
{
	"format": "veroscript-package",
	"formatVersion": "0.1.1",
	"name": "demo-math",
	"version": "0.1.1",
	"entries": {
		"modules": {
			"demo/math.vs": "scripts/math.vs"
		}
	}
}
```

### library/scripts/math.vs

```VeroScript
export int answer()
{
	return 42;
}
```

### app/veroscript-project.json

```json
{
	"format": "veroscript-project",
	"formatVersion": "0.1.1",
	"name": "demo-app",
	"version": "0.1.1",
	"dependencies": {
		"demo-math": {
			"version": "0.1.1",
			"archive": "../demo-math-0.1.1.vspkg"
		}
	}
}
```

### app/main.vs

```VeroScript
import "demo/math.vs";
int main()
{
	print(answer());
	return 0;
}
```

## 2. パック、導入、実行

演習フォルダーから実行します。最後のスクリプトが 42 を表示し、install/list/verify は先にそれぞれの結果を表示します。アーカイブは ZIP の導入用配布物で、.vslib ではありません。

```sh
veroscript pack ./library --output ./demo-math-0.1.1.vspkg
veroscript install --path ./app --offline
veroscript list --path ./app --json
veroscript verify --path ./app
veroscript --backend=vm --path ./app ./app/main.vs
```

## 3. 確認して変更する

pack は既存の出力を上書きしません。演習の旧アーカイブを別名へ移してから、元のパスへ再パックしてください。

answer() を 43 にして再パックします。再導入するまでは導入済み不変スナップショットが 42 を返します。--locked は変更されたアーカイブを検出し、別の内容を黙って採用しません。ソース変更時は意図するロックを復元または再生成してください。

## 4. 次の手順

導入だけでは import されず、main.vs が demo/math.vs を明示的に import します。コンパイル専用依存は実行時配置に含めません。更新のため受領記録/ロックを削除したり導入ファイルを編集しないでください。

[リファレンス](../../../../../VeroScriptPackageManager/Resources/Docs/User/ja-JP/index.md)

<!-- docs-navigation -->

---

[チュートリアル](../index.md)

言語: [en-US](../../en-US/toolchain/21-package-manager.md) · [fr-FR](../../fr-FR/toolchain/21-package-manager.md) · [ja-JP](21-package-manager.md) · [ko-KR](../../ko-KR/toolchain/21-package-manager.md) · [zh-CN](../../zh-CN/toolchain/21-package-manager.md) · [zh-TW](../../zh-TW/toolchain/21-package-manager.md)
