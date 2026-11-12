# 24: Use collections, navigation and dialogs

Open the supplied GUI example, select items, sort a virtual table and return a modal-dialog result. The example contains 10,000 list items and table rows while realizing only the viewport rows.

## 1. Prepare

Use a matching GUI-enabled Runtime with its shipped `Vm/` directory and configured `sdl3` platform/graphics, `rmlui` GUI, `stb` image, and `font-backend` font/text-layout providers. Work inside an existing GUI project whose installed environment supplies these services; copying source files does not install providers. Keep the complete Runtime and provider resources available.

Copy all three linked files into the same writable project directory: [main.vs](../../../../../Modules/Multimedia/VeroScriptGui/Resources/Examples/CompositeControls/main.vs), [page.vswml](../../../../../Modules/Multimedia/VeroScriptGui/Resources/Examples/CompositeControls/page.vswml) and [page.vswcss](../../../../../Modules/Multimedia/VeroScriptGui/Resources/Examples/CompositeControls/page.vswcss). Keep their names and relative layout. Open a terminal in that directory.

## 2. Check

The commands below set the compile-processor timeout to 20 seconds; instruction limits and other host policies still apply. Adjust those budgets for the project and host. A successful check returns zero without opening a window or executing `main`.

```sh
veroscript --compiler-timeout 20000 --platform-backend sdl3 --graphics-backend sdl3 --gui-backend rmlui --image-backend stb --font-backend font-backend --text-layout-backend font-backend gui-check main.vs
```

## 3. Run and interact

Preview the script entry with the same options. It calls `Application.Run()`; close the window to exit. `--frames` is accepted only for ML preview entries; using it with this `.vs` entry returns a usage error.

```sh
veroscript --compiler-timeout 20000 --platform-backend sdl3 --graphics-backend sdl3 --gui-backend rmlui --image-backend stb --font-backend font-backend --text-layout-backend font-backend gui-preview main.vs
```

In ListView and TreeView, select a row and navigate with arrows. Change ComboBox selection and TabView pages. Click a table header to sort its text; sorting is ordinal UTF-8, so numeric-looking text is not numeric sorting. Open the File menu and its dialog, then choose accept/cancel or press Escape; the status text shows the result. While the dialog is open, background controls do not receive normal pointer/keyboard input.

## 4. Observe virtualization

Scroll the two 10,000-row views. Logical rows remain in the data source while widgets are recycled. Selection belongs to the key, not the recycled widget instance. The example uses a fixed 28-unit row height; custom content must fit it. Variable-height rows and virtualized trees are outside this feature.

## 5. Exercise

Change the labels or status strings and repeat the check. Increase the source row count while keeping `count × rowHeight ≤ 16,777,216`; observe scrolling again. For custom row factories, fully reset recycled state and use stable nonempty unique keys. See [Collections, navigation and dialogs](../../../../../Modules/Multimedia/VeroScriptGui/Resources/Docs/User/en-US/Reference/Framework.md) for `SetVirtualSource`, overscan, ownership and selection APIs.

<!-- docs-navigation -->

---

[Tutorials](../index.md)

Language: [en-US](24-gui-collections.md) · [fr-FR](../../fr-FR/toolchain/24-gui-collections.md) · [ja-JP](../../ja-JP/toolchain/24-gui-collections.md) · [ko-KR](../../ko-KR/toolchain/24-gui-collections.md) · [zh-CN](../../zh-CN/toolchain/24-gui-collections.md) · [zh-TW](../../zh-TW/toolchain/24-gui-collections.md)
