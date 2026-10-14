# 第一週工作清單

繳交兩項：**提案 PDF（3～5 頁）**，以及放在 PDF 裡的 **30～60 秒 YouTube 驗證影片連結**。

## 1. 燒錄驗證程式（成員 A）

1. 把 `focus_check/secrets.h.example` 複製成 `focus_check/secrets.h`，填入 Wi-Fi 帳號密碼（這個檔不要繳交）
2. 燒錄：按住 UART_DOWNLOAD、按一下 RESET、放開，然後執行
   `powershell -ExecutionPolicy Bypass -File scripts/amb82.ps1 upload -Port COM6`
3. 按 RESET 開始執行，再開序列埠：`scripts/amb82.ps1 monitor -Port COM6`
4. 用 VLC「開啟網路串流」，輸入序列埠印出的 `rtsp://...` 網址，就能看到偵測框和左上角的狀態文字
5. 序列埠預期會出現：
   ```
   [12345 ms] PERSON -> PHONE (person=82 phone=61)
   [13000 ms] state=PHONE person=80 phone=63 frames/s=9
   ```

## 2. 驗證並記錄真實數據（成員 A＋C）

把以下情況各做一次，將序列埠上**實際**的分數和 frames/s 填進 `docs/proposal.html` 第六節的【待填】欄位：

| 情況 | 預期狀態 | LED |
|---|---|---|
| 鏡頭前沒有人 | NO_TARGET | 全滅 |
| 人坐在鏡頭前 | PERSON | 綠燈亮 |
| 拿起手機 | PHONE | 藍燈亮 |
| 手機斜放或用手遮一半 | 可能出現 UNSURE | 藍燈不亮 |

如果某個情況沒有達到預期（例如手機背面朝鏡頭時分數很低），也要照實寫在「發現的問題」欄位。這是第三週改進的材料，不會扣分。

## 3. 錄驗證影片（成員 B），30～60 秒

**事前準備**
- 電腦連 ITILab；攝影機排線確實接好（沒接時序列埠只有開機訊息、沒有 `F,...`）
- VLC 用 TCP 模式開 `rtsp://192.168.100.179:554`（`--rtsp-tcp`），畫面上要看得到偵測框與左上角 `STATE`
- 終端機執行 `powershell -ExecutionPolicy Bypass -File scripts/live_status.ps1`，用顏色顯示板子回傳的辨識結果
- 螢幕左右並排：左 VLC、右終端機；用手機從斜後方拍，同時拍到螢幕與板子 LED（或用 Win＋Shift＋S 錄螢幕，另外用手機補拍 LED）
- 手機螢幕朝向鏡頭時最容易被偵測（第一週驗證發現）

**腳本**

| 秒數 | 動作 | 畫面上應看到 |
|---|---|---|
| 0～5 | 旁白：「讀書專注小幫手第一週驗證，辨識在 AMB82-MINI 板上執行，電腦只負責顯示」 | — |
| 5～15 | 離開鏡頭 | 沒有目標、燈全滅 |
| 15～25 | 坐到鏡頭前 | 有人、綠框、綠燈亮 |
| 25～40 | 拿起手機滑 | 偵測到手機、紅框、藍燈亮 |
| 40～50 | 手機斜放或用手遮一半 | 不確定或分數下降，藍燈不亮 |
| 50～60 | 放下手機、離開 | 回到有人，再回到沒有目標 |

上傳 YouTube 建議設為「不公開」，再把連結貼進 PDF。只拍組員本人，不要拍到路人。

## 4. 完成 PDF（成員 C）

1. 在 `docs/proposal.html` 搜尋「【」，把所有待填欄位補上：三位成員的姓名學號、YouTube 連結、驗證結果
2. 如果想改構想比較的自評分數或分工，直接改 HTML
3. 產生 PDF：`powershell -ExecutionPolicy Bypass -File scripts/build_proposal.ps1`，輸出到 `submission/Week1_Proposal.pdf`
4. 確認還是 3～5 頁，並把 PDF 裡的 YouTube 連結點一次，確認打得開

## 5. 每個人都要能說明的部分

- A：`focus_check.ino` 的 `classify()` 為什麼分四種狀態、門檻為什麼設 50 和 30
- B：流程圖裡每個判斷框是什麼意思，「無目標」和「不確定」有什麼不同
- C：六個成功標準分別要怎麼量測、為什麼選構想 A
