# multipart 上傳與解析

`FMultipartReader::TryCreate(content_type, out, error, options, context)` 解析邊界，失敗時保留 `out`。原生 context 可省略；提供時，有界儲存及保留的不可變事件在交付後仍計入使用量。複本共享資料，事件在 context 關閉後仍可讀，但 reader 不再接受新工作。`Feed(bytes, final, event, work)` 回傳已消費位元組數及最多一個事件；未消費尾段須沿用同一 `final` 重新提交。只有 `EventReady` 改變 `event`。`final` 表示真正 EOF，而借用的 work 僅在本次呼叫中檢查取消與預算。

狀態0 NeedInput/1 EventReady/2 Done/3 Failed（須新reader）。事件0 PartBegin(index/head)/1 Data(非空≤16 KiB/context)/2 PartEnd/3 End；End 仍須排 HTTP epilogue 至 EOF。head 保名稱、filename缺少/空的區別、類型與重複頭順序。form-data 要單一明確 Disposition/form-data/name（空可），其他子類可無name，巢狀仍raw。頭大小寫無關、折行空白，重複參數/模糊type拒絕；保 NUL/編碼/順序，不自動 charset/percent/transfer decode，不按filename讀寫檔，處理跨塊/空部分/前後導/填補。

預設256部分，頭64 KiB/部分及1 MiB總、正文64 MiB/部分及256 MiB總、含framing輸入272 MiB、前後導各64 KiB。限額獨立、不聚合整份，長期持有事件仍可能Limit。腳本8個max_*、readerfeed/state/event/error及eventkind/part_index/name/has_filename/filename/content_type/header_count/header/data有型別檢查。先處理事件/尾段，HTTP End後final至Done；VM取消使呼叫失敗，普通格式錯存reader。

上傳 Body/單次Upload/原生factory互斥，已知長界定送出範圍、不足Protocol、超出不送，未知長需Finish，WouldBlock不等EOF。混合/重複/binary保留；重放各流需I/O即時新factory、無腳本/阻塞、長度形態一致。單消費且各終態取消不用來源。單次/總Upload不含MIMEframing，buffer預扣、流按接受，metadata/buffer計context、pipe有界另計。後端生成Content-Type/Length/Transfer-Encoding。腳本begin_multipart/add_field/add_file/*_stream、length=-1、256部分/metadata+buffer16 MiB以內，無scriptI/Ofactory。檔案配接已提供，filename本身不存取磁碟。

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../../en-US/Reference/multipart.md) · [fr-FR](../../fr-FR/Reference/multipart.md) · [ja-JP](../../ja-JP/Reference/multipart.md) · [ko-KR](../../ko-KR/Reference/multipart.md) · [zh-CN](../../zh-CN/Reference/multipart.md) · [zh-TW](multipart.md)
