# RPA-Block — 專案需求規格書

C++ 桌面 RPA Low-Code 撰寫器（Windows）。本文件為 v1 需求規格，與 [WIREFRAME.md](WIREFRAME.md) 互相對照。

---

## 1. 產品概述

RPA-Block 是一套桌面應用程式，讓使用者以「低程式碼」方式建立、執行、發佈 RPA（Robotic Process Automation）流程：

- 以**步驟卡片**組裝流程，不需要寫程式
- 以 **OCR 文字錨點**或 **OpenCV 影像模板**定位畫面物件，不依賴螢幕座標
- 內建 **AI 聊天室**，用自然語言描述需求即可生成流程
- 支援**錄製模式**：實際操作一遍，AI 自動整理成正式流程
- 內嵌 **REST API server**，讓外部系統以 HTTP 觸發已發佈的流程

### 技術決策

| 項目 | 選擇 | 備註 |
|---|---|---|
| 語言 / 建置 | C++20 / CMake ≥ 3.24 | vcpkg 管理相依 |
| GUI | Qt 6（Widgets） | LGPL 動態連結 |
| OCR | PaddleOCR（PP-OCR v4 模型） | 以 ONNX Runtime 部署，簡化 C++ 整合；繁中辨識佳 |
| 影像比對 | OpenCV（`matchTemplate`） | 找非文字物件（圖示、按鈕）；同時負責截圖前處理 |
| REST server | cpp-httplib | header-only，內嵌於主程式 |
| AI 服務 | structured-multimodal-agent | LangGraph，`https://agents.scfg.io/<agent>/runs/stream` |
| 目標平台 | Windows 10 / 11 x64 | 輸入模擬走 `SendInput`，元素資訊走 UI Automation |

---

## 2. 功能需求

### FR-1 流程編輯器（Low-Code）

| ID | 需求 | 驗收條件 |
|---|---|---|
| FR-1.1 | 以步驟卡片直列呈現流程，支援拖曳排序、插入、刪除、停用 | 拖曳後 IR 中 steps 順序同步更新 |
| FR-1.2 | 左側工具箱列出所有步驟型別，拖入或雙擊即加入流程 | 見 §3 步驟型別表 |
| FR-1.3 | 右側屬性面板編輯選中步驟的參數 | 參數即時寫回 IR，非法值以紅框提示 |
| FR-1.4 | 流程以 JSON 檔（`.rpa.json`）儲存 / 開啟 | 格式見 §3 |
| FR-1.5 | ▶ 執行 / ⏸ 暫停 / ⏹ 停止當前流程，執行中的步驟卡片高亮 | 執行紀錄寫入底部日誌面板 |
| FR-1.6 | 單步執行（step over）供除錯 | 每按一次執行一個步驟 |

### FR-2 OCR / 影像找物件

| ID | 需求 | 驗收條件 |
|---|---|---|
| FR-2.1 | `ocr_find` 步驟：截圖 → OCR → 依錨點文字定位，回傳中心座標 | 支援完全比對 / 包含 / regex 三種比對模式 |
| FR-2.2 | `image_find` 步驟：OpenCV `matchTemplate` 依模板圖定位 | 相似度門檻可調（預設 0.85），支援多尺度比對 |
| FR-2.3 | 目標選取器：全螢幕截圖 overlay，框選區域後即時顯示 OCR 結果或存成模板圖 | 見 wireframe 畫面 3 |
| FR-2.4 | 兩種定位皆支援 `offset_x` / `offset_y`（相對命中中心的點擊偏移） | 例：找到「登入」文字後點擊其右方 80px 的按鈕 |
| FR-2.7 | `relative` 目標：以標籤為錨點指定方位與控制項種類 | 兩級退化：先比對控制項自身名稱（零座標），再以標籤幾何關係推算；候選須在垂直或水平方向與錨點重疊，避免選到鄰列欄位；失敗須區分「錨點不存在／該種控制項未暴露／方向範圍內無物」 |
| FR-2.5 | 找不到目標時依步驟設定：重試（次數＋間隔）→ 逾時後失敗或走 `on_fail` 分支 | 預設重試 3 次、間隔 1s |
| FR-2.6 | 搜尋範圍可限定：全螢幕 / 指定視窗 / 指定矩形區域 | 縮小範圍以提升速度與準確度 |

### FR-3 REST API 發佈

| ID | 需求 | 驗收條件 |
|---|---|---|
| FR-3.1 | 內嵌 HTTP server，可於 API 面板啟停、設定 port（預設 8420） | 見 §5 API 規格 |
| FR-3.2 | 流程可標記為「已發佈」，僅已發佈流程可被 API 觸發 | 未發佈流程呼叫回 404 |
| FR-3.3 | API key 認證（`X-API-Key` header），可於面板產生 / 撤銷多把 key | 無效 key 回 401 |
| FR-3.4 | 觸發執行為非同步：回 `run_id`，以查詢端點取得狀態 / 結果 | 同一時間僅允許一個流程執行（桌面獨占），排隊或回 409 可設定 |
| FR-3.5 | 執行歷史保留於本地（SQLite 或 JSON lines），面板可查看 | 含觸發來源、起訖時間、結果、失敗步驟 |
| FR-3.6 | 流程失敗時自動擷取當下畫面存於本地，執行歷史記錄其路徑 | API 回報路徑而非提供下載：截圖為整個桌面，可能含無關資訊 |
| FR-3.7 | 可選的對外通道，支援 ngrok 與 Cloudflare Tunnel，使用者自備 token | agent 不隨程式散布（ngrok 條款限制）；**token 為必填，未填不得開啟**；token 依供應者分開存於 Credential Manager 並以環境變數傳遞（不進命令列）；需明確確認且無 API 金鑰時不得開啟；開啟時主視窗常駐警示 |
| FR-3.11 | 通道連上後驗證公開網址確實連回本機 | Cloudflare token 模式的本機位址在後台設定，指錯時通道會連上但完全不通且不報錯；驗證失敗僅警告，不關閉通道 |
| FR-3.8 | API 金鑰以作業系統亂數源產生 | 不得以 PRNG 加種子產生：種子熵會成為金鑰實際強度的上限 |
| FR-3.9 | 認證失敗速率限制 | 60 秒內逾 10 次失敗後，失敗請求回 `429` + `Retry-After`；僅計失敗且僅擋失敗，持有效金鑰者不受影響（否則成為對擁有者的阻斷手段）|
| FR-3.10 | 通道可設定自動開啟與斷線重連 | 供長期掛於外網的機器；預設關閉；無 API 金鑰時不自動開啟；手動關閉須一併取消待重連 |

### FR-4 AI 聊天生成 RPA

| ID | 需求 | 驗收條件 |
|---|---|---|
| FR-4.1 | 右側 dock 聊天室：訊息串、輸入框、附截圖按鈕 | 支援多輪對話，歷史保存在專案檔旁 |
| FR-4.2 | AI 回覆包含「流程草稿」時，顯示**流程差異預覽**與「套用到流程」按鈕 | 套用前不動現有流程；套用＝以 AI 回傳的 IR 取代 / 合併 |
| FR-4.3 | 傳送時自動附帶脈絡：目前流程 IR、（可選）目前螢幕截圖 data URI | 使用者可勾選是否附截圖 |
| FR-4.4 | AI 輸出以 `output_schema` 強制為合法 RPA IR（見 §4），不解析自由文字 | `structured_output` 直接反序列化為流程 |
| FR-4.5 | SSE 串流顯示進度；逾時（預設 120s）與失敗可重試 | 顯示 `usage_cost` 供成本追蹤 |
| FR-4.6 | 草稿有步驟驗證不過時，自動連同錯誤送回 AI 修正，上限 2 次 | 目標使用者無法手改 `params_json`；連續兩次錯誤相同即停止（未收斂），每輪成本累加顯示 |
| FR-4.7 | 執行失敗後可一鍵請 AI 診斷 | 送出失敗步驟、錯誤、流程 IR、日誌尾段與失敗當下截圖；回覆走 FR-4.2 的預覽／套用閘門 |

### FR-5 錄製轉 AI 生成

| ID | 需求 | 驗收條件 |
|---|---|---|
| FR-5.1 | ⏺ 錄製鈕：主視窗最小化，浮動工具列顯示錄製狀態與事件數 | Esc 或 ⏹ 停止 |
| FR-5.2 | 錄下低階滑鼠 / 鍵盤事件（`SetWindowsHookEx` WH_MOUSE_LL / WH_KEYBOARD_LL） | 不掉事件；連續按鍵合併為字串 |
| FR-5.3 | 每次點擊自動擷取：點擊位置周邊截圖、UIA 元素資訊（name / control type / window） | 截圖供 AI 與模板比對使用 |
| FR-5.4 | 停止後顯示錄製結果清單，可刪除多餘事件 | 事件可逐筆檢視縮圖 |
| FR-5.5 | 「交給 AI 整理」：事件摘要＋關鍵截圖送 AI，回傳正式 IR 草稿進入 FR-4.2 預覽流程 | AI 應將座標點擊轉為 `ocr_find` / `image_find` 錨點步驟 |

---

## 3. RPA Script IR（流程定義格式 v1）

副檔名 `.rpa.json`。頂層：

```json
{
  "name": "invoice-download",
  "version": 1,
  "description": "登入 ERP 並下載本月發票",
  "variables": { "username": "demo" },
  "steps": [ ... ]
}
```

每個 step 共通欄位：`id`（string，流程內唯一）、`type`、`enabled`（預設 true）、`comment`、`on_fail`（`"abort"` | `"continue"` | `"goto:<step_id>"`，預設 abort）。

### 步驟型別

| type | 參數 | 說明 |
|---|---|---|
| `click` | `target`, `button`(left/right/middle), `count`(1/2) | 點擊目標 |
| `double_click` | `target` | 等同 `click` count=2 的語法糖 |
| `type_text` | `text`, `interval_ms` | 輸入文字（支援 `{{variable}}` 展開） |
| `key_press` | `keys`（如 `"ctrl+s"`, `"enter"`） | 組合鍵 |
| `wait` | `ms` | 固定等待 |
| `ocr_find` | `text`, `match`(exact/contains/regex), `region?`, `offset_x?`, `offset_y?`, `retry?` | OCR 文字錨點定位，結果存入內建變數 `last_match` |
| `image_find` | `template`（相對專案的圖檔路徑）, `threshold`(0–1), `region?`, `offset_x?`, `offset_y?`, `retry?` | OpenCV 模板比對定位 |
| `launch_app` | `path`, `args?`, `working_dir?` | 開啟應用程式。`path` 也可以是文件、資料夾或網址（走系統關聯），支援 `%ENV%` 展開；只負責啟動，不等程式結束。啟動成功時把 PID 寫入內建變數 `last_launch_pid` |
| `window_activate` | `title_match`, `match`(exact/contains/regex) | 啟用指定視窗 |
| `screenshot` | `path`, `region?` | 截圖存檔 |
| `if` | `condition`（`ocr_found` / `image_found` / `var_equals`…）, `then_steps`, `else_steps?` | 條件分支 |
| `loop` | `count` 或 `while_condition`, `steps` | 迴圈 |
| `http_request` | `method`, `url`, `headers?`, `body?`, `save_to_var?` | 呼叫外部 API |

`target` 物件（`click` / `double_click` 使用）三擇一：

```json
{ "kind": "ocr",   "text": "登入", "match": "exact", "offset_x": 0, "offset_y": 0 }
{ "kind": "image", "template": "assets/login-btn.png", "threshold": 0.85 }
{ "kind": "point", "x": 512, "y": 300 }   // 僅供錄製原始資料，AI 整理後應轉為前兩者
```

`retry` 物件：`{ "times": 3, "interval_ms": 1000 }`。

---

## 4. AI 整合契約（structured-multimodal-agent）

### 4.1 端點與認證

- `POST https://agents.scfg.io/structured-multimodal-agent/runs/stream`（SSE，`stream_mode: ["values"]`，取**最後一個** `values` 事件的 `structured_output` 與 `usage_cost`）
- 認證二擇一，於設定頁配置：
  - `Authorization: Bearer <jwt>`（`POST https://api.cluster.scfg.io/public/auth/signin` 換取）
  - `X-API-Key: <teamsync-user-api-key>`
- 可選 `provider`（`gemini` / `claude`）與 `model` 覆寫，同樣於設定頁配置。

### 4.2 訊息組裝

依 API 契約，`messages` 為 user 訊息串；多輪聊天由客戶端自行維護，每次請求將先前對話（含 AI 上一版流程摘要）摺疊進 user content。多模態區塊：

```json
{
  "role": "user",
  "content": [
    { "type": "text", "text": "<系統脈絡>目前流程 IR：{...}\n<使用者需求>幫我在登入後多加一步下載報表" },
    { "type": "image_url", "image_url": { "url": "data:image/png;base64,..." } }
  ]
}
```

`system_prompt` 欄位放入固定的 RPA 領域指示（步驟語意、優先使用 ocr/image 錨點而非座標、繁中回覆等）。

### 4.3 output_schema（RPA IR 的結構化輸出定義）

`structured_output` 直接是可執行流程草稿 + 給使用者的說明。核心 schema（符合 `OutputSchemaV1`，`version: 1`）：

```json
{
  "version": 1,
  "fields": [
    { "name": "reply", "type": "str", "description": "給使用者的繁中說明（做了什麼、為什麼）" },
    { "name": "has_script", "type": "bool", "description": "本回合是否產出流程草稿" },
    { "name": "script_name", "type": "str", "optional": true, "max_length": 100 },
    {
      "name": "steps", "type": "list", "optional": true,
      "description": "RPA 步驟列表，語意見系統提示",
      "items": {
        "type": "described_object",
        "entries": [
          { "key": "id",   "value": { "type": "str", "max_length": 40 } },
          { "key": "type", "value": { "type": "literal_str",
            "values": ["click","double_click","type_text","key_press","wait",
                        "ocr_find","image_find","window_activate","screenshot",
                        "if","loop","http_request","launch_app"] } },
          { "key": "params_json", "key_description": "該步驟參數，序列化為 JSON 字串",
            "value": { "type": "str" } },
          { "key": "comment", "value": { "type": "str", "optional": true } }
        ]
      }
    }
  ]
}
```

> 註：巢狀 `if` / `loop` 的子步驟包在 `params_json` 內（`OutputSchemaV1` 不支援遞迴定義）。客戶端收到後反序列化 `params_json` 並以 §3 的 IR schema 驗證，驗證失敗即標示該步驟需人工修正，不阻擋整份草稿預覽。

### 4.4 錄製轉流程的請求

錄製事件先在本地壓縮成摘要（點擊 → 附近截圖的 OCR 文字 / UIA name、鍵入合併字串、間隔時間），連同 2–5 張關鍵截圖送出，要求 AI 產出以錨點（`ocr_find` / `image_find`）取代裸座標的正式流程。

### 4.5 逾時與重試

- 單次請求逾時 120s；SSE 中斷或 `error` 事件 → 提示重試（不自動重送，避免重複計費）
- 每回合顯示 `usage_cost.cost`（USD）於聊天室訊息尾端

---

## 5. REST API 規格（內嵌 server）

Base：`http://<host>:8420/api/v1`，全部端點需 `X-API-Key` header（面板產生）。

| Method | Path | 說明 | 回應 |
|---|---|---|---|
| GET | `/scripts` | 列出已發佈流程 | `[{ id, name, description, published_at }]` |
| POST | `/scripts` | 上傳 / 更新流程（body = IR JSON） | `{ id }` |
| POST | `/scripts/{id}/run` | 非同步觸發執行，body 可帶 `variables` 覆寫 | `202 { run_id }`；執行中回 `409` |
| GET | `/runs/{id}` | 查詢執行狀態 | `{ run_id, script_id, status: queued/running/succeeded/failed, current_step, started_at, finished_at, error? }` |
| GET | `/runs` | 執行歷史（分頁） | 陣列 |
| GET | `/health` | 健康檢查（免認證） | `{ status: "ok", version }` |

安全限制：預設僅綁定 `127.0.0.1`；綁 `0.0.0.0` 需在面板明確開啟並顯示警告。

---

## 6. 系統架構

```
RPA-Block/
├─ rpa-core/     # 無 UI 依賴靜態庫：Script IR（解析/驗證/序列化）、Executor
│                #   Executor：Win32 SendInput 滑鼠鍵盤、UI Automation 視窗操作
├─ rpa-vision/   # 截圖(DXGI/GDI) + PP-OCR(ONNX Runtime) + OpenCV matchTemplate
├─ rpa-recorder/ # SetWindowsHookEx 低階 hook + 逐點擊截圖 + UIA 元素資訊
├─ rpa-ai/       # structured-multimodal-agent client（QNetworkAccessManager, SSE 解析）
├─ rpa-server/   # cpp-httplib REST server（獨立執行緒，經佇列與 Executor 溝通）
├─ rpa-studio/   # Qt 6 主程式：流程編輯器、聊天室、目標選取器、API 面板、設定
└─ tests/        # rpa-core IR 往返測試、vision 定位測試（固定測試圖）、server 端點測試
```

執行緒模型：UI 主執行緒（Qt）；Executor 專用執行緒（避免阻塞 UI，透過 signal 回報進度）；REST server 執行緒（觸發請求進入佇列，由 Executor 執行緒消化）；錄製 hook 於獨立 message-loop 執行緒。

## 7. 非功能需求

| 項目 | 目標 |
|---|---|
| OCR 單幀（1080p 全螢幕） | < 1s |
| OpenCV 模板比對單幀 | < 200ms |
| 錄製 hook | 不掉事件；hook callback < 1ms（重活丟 queue） |
| AI 請求 | 逾時 120s，串流進度可視 |
| 隱私 | 截圖僅在使用者明示（附截圖鈕 / 錄製）時送出；API key、JWT 存 Windows Credential Manager |
| 打包 | windeployqt + vcpkg 相依收齊，單資料夾綠色版 + NSIS 安裝檔 |

## 8. 里程碑

| 里程碑 | 範圍 | 可驗證成果 |
|---|---|---|
| **M1** | 專案骨架、IR 解析/驗證、Executor 基本動作（click/type/wait/window） | CLI 跑通一支手寫 `.rpa.json` |
| **M2** | rpa-vision（OCR + 模板比對）、目標選取器 UI、`ocr_find`/`image_find` 步驟 | 在記事本 demo「找『檔案』選單並點擊」 |
| **M3** | 錄製器、AI 聊天室、AI 生成/錄製轉流程 | 錄一段操作 → AI 產出可重播流程 |
| **M4** | REST API server、執行歷史、打包 | 外部 curl 觸發流程並查到結果 |
