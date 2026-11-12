# 21: 安裝本機擴充套件

使用啟用套件管理的桌面 Runtime，在空白練習目錄操作。此練習離線完成，不需存放庫伺服器。

## 1. 建立檔案

將套件清單儲存為 library/veroscript-package.json、模組為 library/scripts/math.vs、專案清單為 app/veroscript-project.json、入口為 app/main.vs。保留 archive 相依指向的目錄關係。

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

## 2. 封裝、安裝與執行

從練習目錄執行下列命令。最後指令碼輸出 42，install/list/verify 在此之前輸出各自結果。此封存是 ZIP 安裝分發套件，不是 .vslib。

```sh
veroscript pack ./library --output ./demo-math-0.1.1.vspkg
veroscript install --path ./app --offline
veroscript list --path ./app --json
veroscript verify --path ./app
veroscript --backend=vm --path ./app ./app/main.vs
```

## 3. 觀察與修改

pack 不覆寫既有輸出。再次封裝前，先將練習的舊封存移到其他檔名，再向原路徑產生新封存。

將 answer() 改為 43 並重新封裝。已安裝不可變快照仍回傳 42，直到再次 install。--locked 會發現封存已變，不會默默接受新位元組。修改來源時恢復或重新產生預期鎖定。

## 4. 繼續使用

安裝不會自動匯入模組，main.vs 仍須明確匯入 demo/math.vs。僅編譯相依不應進入執行階段部署。不要刪除收據/鎖檔或修改已安裝內容來更新套件。

[參考文件](../../../../../VeroScriptPackageManager/Resources/Docs/User/zh-TW/index.md)

<!-- docs-navigation -->

---

[教學](../index.md)

語言: [en-US](../../en-US/toolchain/21-package-manager.md) · [fr-FR](../../fr-FR/toolchain/21-package-manager.md) · [ja-JP](../../ja-JP/toolchain/21-package-manager.md) · [ko-KR](../../ko-KR/toolchain/21-package-manager.md) · [zh-CN](../../zh-CN/toolchain/21-package-manager.md) · [zh-TW](21-package-manager.md)
