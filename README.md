[README.md](https://github.com/user-attachments/files/33424381/README.md)
# 讀書專注小幫手（Focus Guard）

用 Ameba AMB82-MINI 與攝影機，在**開發板上**辨識「人」與「手機」，判斷讀書時是否在滑手機，並用 LED 與板上網頁提醒、統計分心次數與時間。

物聯網課程作業「讓 Ameba 看懂生活——影像辨識創意實作」。
組員：余易謙（韌體與模型）、沈子淵（網頁與整合）、徐子豪（測試與文件）

## 資料夾

| 路徑 | 內容 |
|---|---|
| `focus_guard/` | **第二週原型**：狀態機（手機連續 3 秒才算分心、2 秒寬限、3 秒離座）、分心計數與時間統計、板上網頁 |
| `focus_check/` | 第一週基本辨識驗證：只取 person、cell phone，分成四種狀態並控制 LED |
| `scripts/` | 燒錄、測試與顯示用的 PowerShell 腳本（見下方） |
| `docs/` | 提案與進度說明的原始檔、測試說明、實測數據（`docs/test-data/`） |

## 環境

- 硬體：Ameba AMB82-MINI、攝影機模組、USB 線；LED 使用板上內建的 LED_G（D24）與 LED_B（D23）
- 軟體：Arduino IDE 或 arduino-cli，Realtek AmebaPro2 板子套件 4.1.1；開發板選 `Ameba_AMB82-MINI`
- 模型：SDK 內建 YOLOv4-tiny（COCO 80 類），不需要自行訓練

## 使用方式

1. 把 `focus_guard/secrets.h.example` 複製成 `focus_guard/secrets.h`，填入 Wi-Fi 名稱與密碼（這個檔案不會上傳）
2. 燒錄：按住 UART_DOWNLOAD → 按一下 RESET → 放開，然後執行
   ```powershell
   powershell -ExecutionPolicy Bypass -File scripts/amb82.ps1 upload -Port COM6 -SketchName focus_guard
   ```
   （也可以直接用 Arduino IDE 開啟 `focus_guard/focus_guard.ino` 上傳）
3. 按 RESET 開始執行。序列埠（115200）會印出網址：
   - 網頁：`http://<板子IP>/`，顯示狀態、讀書時間、分心次數與紀錄，可按「歸零」
   - 影像：VLC 開啟 `rtsp://<板子IP>:554`（建議加 `--rtsp-tcp`），畫面上有偵測框與狀態
4. LED：綠燈＝讀書中，藍燈＝分心中

## 腳本

| 腳本 | 用途 |
|---|---|
| `scripts/amb82.ps1` | 編譯、燒錄、序列埠監看（`compile` / `upload` / `monitor` / `list`） |
| `scripts/live_status.ps1` | 讀序列埠，以中文與顏色顯示即時狀態與分心事件（錄影用） |
| `scripts/placement_test.ps1` | 分步驟記錄每一張影像的辨識分數並統計（擺放位置與辨識率測試） |
| `scripts/rtsp_probe.ps1` | 不經過 VLC，直接測試板子的 RTSP 是否有送出影像 |
| `scripts/build_proposal.ps1` | 用 Chrome／Edge 把 `docs/` 的 HTML 轉成 PDF |

## 已知限制

- 手機背面朝向鏡頭時幾乎偵測不到；板子建議放在書桌斜前方、高出桌面約 30 公分
- 統計資料存在記憶體，重新開機會歸零；時間以開機後經過的時間表示
- 板上網頁與 RTSP 畫面一次各只適合一個人觀看
- 序列埠只有開機訊息、沒有 `F,...` 辨識紀錄時，先檢查攝影機排線是否接好

## 來源與 AI 使用

- 改自 Realtek Ameba Arduino SDK 範例 `NeuralNetwork/ObjectDetectionCallback`（含 `ObjectClassList.h`）
- 使用生成式 AI（Claude）協助撰寫程式、測試腳本與文件；功能取捨、擺放位置與測試由本組決定並在開發板上實測
