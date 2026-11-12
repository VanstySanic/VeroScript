# 21: 安装本地扩展包

使用启用包管理的桌面 Runtime，在空的练习目录操作。此练习离线完成，不需要仓库服务器。

## 1. 创建文件

将包清单保存为 library/veroscript-package.json，模块为 library/scripts/math.vs，项目清单为 app/veroscript-project.json，入口为 app/main.vs。保留 archive 依赖指向的目录关系。

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

## 2. 打包、安装与运行

从练习目录执行以下命令。最后脚本输出 42，install/list/verify 在此之前输出各自结果。此归档是 ZIP 安装分发包，不是 .vslib。

```sh
veroscript pack ./library --output ./demo-math-0.1.1.vspkg
veroscript install --path ./app --offline
veroscript list --path ./app --json
veroscript verify --path ./app
veroscript --backend=vm --path ./app ./app/main.vs
```

## 3. 观察与修改

pack 不覆盖已有输出。再次打包前，先将练习中的旧归档移到其他文件名，再向原路径生成新归档。

将 answer() 改为 43 并重新打包。已安装不可变快照仍返回 42，直到再次 install。--locked 会发现归档已变，不会静默接受新字节。修改来源时恢复或重新生成预期锁定。

## 4. 继续使用

安装不会自动导入模块，main.vs 仍须显式导入 demo/math.vs。仅编译依赖不应进入运行时部署。不要删除收据/锁文件或修改已安装内容来更新包。

[参考文档](../../../../../VeroScriptPackageManager/Resources/Docs/User/zh-CN/index.md)

<!-- docs-navigation -->

---

[教程](../index.md)

语言: [en-US](../../en-US/toolchain/21-package-manager.md) · [fr-FR](../../fr-FR/toolchain/21-package-manager.md) · [ja-JP](../../ja-JP/toolchain/21-package-manager.md) · [ko-KR](../../ko-KR/toolchain/21-package-manager.md) · [zh-CN](21-package-manager.md) · [zh-TW](../../zh-TW/toolchain/21-package-manager.md)
