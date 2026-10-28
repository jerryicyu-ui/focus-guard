# 讀書專注小幫手（Focus Guard）

用 Ameba AMB82-MINI 與攝影機，在**開發板上**辨識「人」與「手機」，判斷讀書時是否在滑手機；用 LED 與板上網頁提醒，並統計讀書時間、分心次數與分心時間。

物聯網課程作業「讓 Ameba 看懂生活——影像辨識創意實作」。
組員：余易謙（韌體與模型）、沈子淵（網頁與整合）、徐子豪（測試與文件）

## 資料夾

| 路徑 | 內容 |
|---|---|
| `focus_guard/` | **最終版程式**（第二週原型＋第三週改進）：狀態機、分心計數與時間統計、板上網頁 |
| `focus_check/` | 第一週基本辨識驗證程式 |
| `scripts/` | 燒錄、測試、顯示用的 PowerShell 腳本 |
| `docs/` | 提案、進度說明、成果報告的原始檔；`docs/test-data/` 為全部原始測試紀錄 |

## 材料

| 項目 | 數量 | 說明 |
|---|---|---|
| Ameba AMB82-MINI 開發板 | 1 | 影像辨識、狀態判斷、LED、Wi-Fi 網頁都在板上執行 |
| 攝影機模組（JXF37） | 1 | 開發板套件附的攝影機，以排線接在板子的攝影機連接座 |
| USB 傳輸線（可傳資料） | 1 | 供電、燒錄、序列埠 |
| 電腦 | 1 | 燒錄程式；用瀏覽器看網頁、VLC 看影像（只負責顯示） |
| 紙箱或支架 | 1 | 固定板子，位於書桌斜前方、高出桌面約 30 公分 |

## 接線

不需要外接任何電子元件：

- 攝影機排線插入板子的攝影機連接座，**金屬接點方向對準、完全插到底後扣緊卡扣**
- LED 使用板上內建的 **LED_G（D24，綠）** 與 **LED_B（D23，藍）**
- 用 USB 線接電腦即可供電

固定板子時，**不要讓紙箱或膠帶壓到 UART_DOWNLOAD 按鈕**，否則按 RESET 會進入燒錄模式而不會執行程式。

## 軟體與設定

| 項目 | 設定 |
|---|---|
| 開發環境 | Arduino IDE 2 或 arduino-cli |
| 板子套件（SDK） | Realtek AmebaPro2 **4.1.1-build20260915**（Arduino 開發板管理員安裝） |
| 開發板 | `Ameba_AMB82-MINI`（FQBN `realtek:AmebaPro2:Ameba_AMB82-MINI`） |
| Camera Option | **JXF37**（預設值） |
| NN Model Load From | Flash（預設值） |
| 模型 | SDK 內建 **YOLOv4-tiny**（COCO 80 類），不需自行訓練；只使用 person（0）與 cell phone（67） |
| 模型門檻 | 信心值 0.30、NMS 0.30（程式中 `MODEL_MIN_CONFIDENCE`、`MODEL_NMS`） |
| 辨識影像 | 通道 3，576×320 RGB，10 fps（實際辨識約每秒 6 張） |
| 串流影像 | 通道 0，1280×720 H.264，1 Mbps，RTSP（只用於除錯與錄影） |

### 判斷規則（`focus_guard.ino` 開頭的常數）

| 常數 | 值 | 意義 |
|---|---|---|
| `CONFIRM_SCORE` | 50 | 人或手機 ≥ 50 分才算看到；手機要 ≥ 50 才開始確認 |
| `KEEP_SCORE` | 30 | 已在確認期或分心中時，手機 ≥ 30 就算還在（遲滯，第三週改進） |
| `PHONE_CONFIRM_MS` | 2000 | 手機連續出現 2 秒判定分心（第三週由 3 秒縮短） |
| `PHONE_GRACE_MS` | 2000 | 手機消失超過 2 秒才結束分心 |
| `AWAY_MS` | 3000 | 人消失超過 3 秒判定離座 |

## 燒錄

1. 把 `focus_guard/secrets.h.example` 複製成 `focus_guard/secrets.h`，填入 Wi-Fi 名稱與密碼（WPA2 網路，並與電腦在同一個網路；此檔不會上傳）
2. 讓板子進入燒錄模式：**按住 UART_DOWNLOAD → 按一下 RESET → 放開 UART_DOWNLOAD**（藍、綠燈同時亮）
3. 上傳：
   - Arduino IDE：開啟 `focus_guard/focus_guard.ino`，選好開發板與 COM 埠後上傳
   - 或用腳本：`powershell -ExecutionPolicy Bypass -File scripts/amb82.ps1 upload -Port COM6 -SketchName focus_guard`
4. 上傳完成後**只按 RESET** 開始執行

## 操作方式

1. 開機約 10～20 秒後，序列埠（115200）會印出網址：
   - `WEB: http://<板子IP>/`：瀏覽器打開，顯示狀態（離座／讀書中／偵測到手機倒數／分心中）、讀書時間、分心次數、分心時間與最近 20 筆紀錄，可按「歸零」
   - `RTSP: rtsp://<板子IP>:554`：VLC 開啟可看偵測框（建議加 `--rtsp-tcp`，一次只能一個人觀看）
2. LED：**綠燈＝讀書中，藍燈＝分心中**，離座時全滅
3. 使用者坐在書桌前讀書；拿起手機滑超過 2 秒會判定分心、分心次數 +1，放下手機 2 秒後回到讀書中

## 腳本

| 腳本 | 用途 |
|---|---|
| `scripts/amb82.ps1` | 編譯、燒錄、序列埠監看（`compile` / `upload` / `monitor` / `list`） |
| `scripts/trial_test.ps1` | 第三週逐次測試：提示操作、讀板子事件自動判斷，存成測試紀錄 CSV |
| `scripts/live_status.ps1` | 讀序列埠，以中文與顏色顯示即時狀態與分心事件（錄影用） |
| `scripts/placement_test.ps1` | 擺放位置測試（第一週） |
| `scripts/rtsp_probe.ps1` | 不經過 VLC 測試 RTSP 是否送出影像 |
| `scripts/build_proposal.ps1` | 把 `docs/` 的 HTML 轉成 PDF |

## 已知限制

- 手機背面朝向鏡頭時幾乎偵測不到；板子需放在書桌斜前方、高出桌面約 30 公分
- 統計存在記憶體，重新開機會歸零；時間以開機後經過時間表示
- 網頁與 RTSP 一次各只適合一個人觀看；RTSP 只在實驗室網路 ITILab 穩定出畫面
- 序列埠只有開機訊息、沒有 `F,...` 辨識紀錄時，先檢查攝影機排線

## 來源與 AI 使用

- 改自 Realtek Ameba Arduino SDK 範例 `NeuralNetwork/ObjectDetectionCallback`（含 `ObjectClassList.h`）
- 使用生成式 AI（Claude）協助撰寫程式、測試腳本與文件；功能取捨、擺放位置、改進方向與全部測試由本組決定並在開發板上實測
