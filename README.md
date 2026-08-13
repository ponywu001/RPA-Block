# RPA-Block

C++ 桌面 RPA 編輯器，**Scratch 式積木介面、全繁體中文**。流程以 **OCR 文字錨點** 或 **OpenCV 影像模板** 定位畫面元素（而非固定座標），可用 **AI 對話**或**錄製實際操作**產生，並能透過內建 **REST API** 對外觸發。

## 積木介面

積木依功能分色，形狀帶卸口與凸榫，像 Scratch 一樣拼在一起：

| 顏色 | 分類 | 積木 |
|---|---|---|
| 🔵 藍 | 滑鼠 | 點擊、雙擊 |
| 🟣 紫 | 鍵盤 | 輸入文字、按快速鍵 |
| 🟠 橙 | 等待 | 等待 |
| 🟢 綠 | 找畫面 | 找文字（OCR）、找圖片（模板比對） |
| 🔵 青 | 視窗 | 開啟程式、切換視窗、截圖 |
| 🟠 深橙 | 流程控制 | 如果、重複 |
| 🔴 粉 | 網路 | 呼叫 API |

`如果` 與 `重複` 是 **C 型積木**：子積木直接放進它的開口裡，並以「結束如果／結束重複」收尾，所以巢狀結構在畫布上一眼看得出來，不必去翻屬性欄位。拖曳一個 C 型積木會連同它包住的整個子樹一起移動。

積木圖示是用 QPainter 向量繪製而非 emoji —— emoji 在這個字型堆疊下會退化成單色方塊，向量圖示則在任何 DPI 都清晰。

## 文字錨點對不上時

OCR 會把**緊鄰文字的圖示當成字讀進來**。放大鏡圖示會變成 `Q`，所以 Windows 的搜尋框讀出來是 `Q 搜尋` 而不是 `搜尋`。同理，中文和數字之間不會有空格（`使用者名稱 8420` → `使用者名稱8420`）。

因此 OCR 錨點**預設用「包含」而不是「完全相同」**。要確認實際讀到什麼：

```bash
rpa-cli ocr screen.png --find 搜尋 --match exact
```
```
  no match.
  closest: 'Q 搜尋'
  closest: 'Q搜'
```

積木執行失敗時也會列出最接近的幾行，不會只說「107 行都沒對上」。

## 面板不見了怎麼辦

每個面板（積木、積木設定、AI 助手、執行紀錄）標題列右邊都有關閉鈕，按到就會整個消失。要找回來：**外觀 → 面板**，勾選要顯示的；或 **外觀 → 回復預設版面** 全部歸位。

視窗大小和面板配置會記住，下次開啟沿用。若儲存的位置落在已不存在的螢幕上，會自動回到主螢幕 —— 標題列拖不到的視窗是救不回來的。

## 亮色與暗色主題

「外觀」選單可選**跟隨系統／亮色／暗色**，也能用 `Ctrl+Shift+L` 直接切換。選「跟隨系統」時會跟著 Windows 的深淺色設定即時變化，不需要重開程式。

介面色彩全部走語意化 token（`surface`、`textMuted`、`accent`、`danger`…），定義在 [Theme.cpp](rpa-studio/src/Theme.cpp)。這不只是整理 —— 原本 49 處硬寫的色值散在 9 個檔案裡，那正是它沒辦法有暗色模式的原因。

強調色是深靛藍，刻意避開積木已經用掉的七個色相：積木才是畫面的主角，介面不該跟它搶。

### 積木文字的對比度

替暗色主題調整積木配色時，我順手量了對比度，結果發現**原本的亮色主題有問題**：所有積木都用白字，但白字在這些高飽和底色上多半不合格。

| 積木 | 原本（白字） | 現在 |
|---|---|---|
| 等待（橘） | **1.89:1** ✗ | 8.43:1 深字 |
| 視窗（青） | 2.12:1 ✗ | 7.73:1 深字 |
| 流程控制（橙） | 2.33:1 ✗ | 7.08:1 深字 |
| 找畫面（綠） | 2.39:1 ✗ | 6.78:1 深字 |
| 網路（粉） | 2.81:1 ✗ | 6.39:1 深字 |
| 滑鼠（藍） | 2.93:1 ✗ | 5.98:1 深字 |
| 鍵盤（紫） | 3.68:1 | 5.05:1 深字 |

七個裡有六個低於 WCAG 大字級的 3:1 下限。修法不是把配色改暗（那會毀掉 Scratch 式的鮮豔感，而鮮豔正是色彩編碼的意義），而是**依底色亮度決定字色** —— `inkOn()` 會比較白字與深字哪個對比更高再選。現在最差的一格是 4.23:1，連 4.5:1 的一般字級標準都幾乎全數通過。

這件事用眼睛看不出來 —— 白字在橘底上「看起來還行」，要量了才知道差多少。

- 規格：[docs/REQUIREMENTS.md](docs/REQUIREMENTS.md)
- 畫面線框：[docs/WIREFRAME.md](docs/WIREFRAME.md)

## 模組

| 模組 | 內容 | 相依 |
|---|---|---|
| `rpa-core` | Script IR（解析／驗證／序列化）、Executor（`SendInput`、視窗啟用、WinHTTP） | 無外部相依（vendored nlohmann/json） |
| `rpa-vision` | 螢幕擷取（GDI）、PP-OCR 推論、OpenCV 模板比對 | OpenCV、ONNX Runtime |
| `rpa-recorder` | 低階滑鼠／鍵盤 hook、逐點擊截圖、UI Automation 元素 | Windows SDK |
| `rpa-ai` | structured-multimodal-agent 客戶端（SSE、`output_schema`） | Qt 6 Network（schema 部分無相依） |
| `rpa-server` | REST API、發佈庫、執行歷史 | vendored cpp-httplib |
| `rpa-studio` | Qt 6 桌面應用（積木畫布、五個畫面） | Qt 6 Widgets |
| `rpa-pack` | 單一 exe 的自解壓縮 stub | — |
| `rpa-cli` | 無頭 `validate` / `show` / `run` / `serve` | — |

`rpa-core` 刻意不依賴 OpenCV、ONNX Runtime 或 Qt：它透過 `ITargetLocator`、`IInputBackend`、`IWindowBackend` 三個介面與外界互動，所以在沒有這些 SDK 的機器上仍可編譯與測試。`rpa-ai` 同理拆成兩個目標 —— `rpa-ai-schema`（`output_schema` 與 prompt，零外部相依）與 `rpa-ai`（Qt 網路層），讓最容易出錯的契約部分不必為了測試而先安裝 Qt。

## 快速開始：做出一個可執行的 exe

三步。需要 **VS 2022 Build Tools**（含 C++ 工作負載）與 **Python 3**。

```
:: 1. 取得三個預編譯相依（約 5 分鐘，不需編譯任何相依）
powershell -ExecutionPolicy Bypass -File fetch-deps.ps1

:: 2. 建置全部模組
build-full.cmd

:: 3. 打包成單一 exe
powershell -ExecutionPolicy Bypass -File package.ps1
```

產出 `dist\RPA-Block.exe` —— **單一 65 MB 檔案，雙擊即可執行**。Qt、OpenCV、ONNX Runtime 和 **OCR 模型**全部在裡面，沒有任何要另外下載或設定的東西。

首次啟動會解壓到 `%LOCALAPPDATA%\RPA-Block\<payload-key>\`（解開後 138 MB，26 個檔案），之後直接用快取：

| | 實測（各三次取範圍） |
|---|---|
| 冷啟動（含解壓 64 MB） | 2.9 – 6.8 秒 |
| 熱啟動（快取命中） | 1.4 – 1.5 秒 |

（量到視窗標題出現為止。冷啟動的範圍主要是磁碟變異；剛打包完立刻量會明顯偏高，那是磁碟還在寫入，不是啟動成本。）

快取目錄以 payload 的 checksum 命名，所以重新打包後會自動用新目錄，不會誤用舊版本。解壓完成才寫入 `.unpacked` 標記檔 —— 中途被打斷的解壓不會留下標記，下次啟動會捨棄該目錄重解，而不是執行一份半殘的安裝（這個復原路徑有實測過）。

## 安裝檔（交機用這個）

```
powershell -ExecutionPolicy Bypass -File package-installer.ps1
```

產出 `dist\RPA-Block-Setup.exe`（49.5 MB）。需要 **NSIS**（`winget install NSIS.NSIS`）。

兩種包裝的內容完全一樣，差別只在怎麼落地：

| | 單一 exe | 安裝檔 |
|---|---|---|
| 大小 | 65 MB | 49.5 MB（LZMA 壓得更好） |
| 落地位置 | 首次執行解壓到 `%LOCALAPPDATA%` | `C:\Program Files\RPA-Block` |
| 啟動 | 冷 2.9–6.8 秒／熱 1.4–1.5 秒 | **1.8 秒**（不必解壓） |
| 捷徑、解除安裝 | 無 | 開始功能表＋桌面（可選）、控制台可解除安裝 |
| 權限 | 不需要 | 需要系統管理員 |
| 適合 | 給人試跑，什麼都不用裝 | 客戶部署 |

實測靜默安裝（`/S`）、啟動、解除安裝三輪都通過，解除後目錄、捷徑、登錄機碼都清乾淨。**解除安裝不會動使用者的流程檔** —— 那些在 Documents 底下，刪掉客戶的自動化不叫乾淨。

### 重裝一次還是升級？

對著已安裝的機器再跑一次安裝檔，是**升級**：它會問你要不要先移除舊版，答應後**先靜默解除安裝、再裝新的**（`/S` 靜默安裝時不問，直接照做）。

先移除而不是直接覆蓋，是因為覆蓋會把「舊版有、新版沒有」的檔案永遠留在目錄裡，而**執行檔旁邊的舊 DLL 會優先於系統路徑被載入** —— 那不只是垃圾，是會讓新版跑到舊程式碼的東西。

實測 0.1.1 → 0.1.2：事先在安裝目錄放一個假的舊版 DLL，升級後檔案數從 36 回到 35，那支被清掉了。

**流程檔、錄製、設定都不會被動到** —— 它們在 Documents 底下和 HKCU 裡，不在安裝目錄。

程式正在執行時安裝檔會擋下來（結束碼 2）並要求先關閉，不會裝到一半留下壞掉的目錄。

版本號是 `package-installer.ps1` 的 `-Version` 參數，預設 `0.1.0`。**出貨前記得帶上真正的版本號**，否則控制台永遠顯示同一個版本，看不出客戶手上是哪一版。

安裝檔上線前踩到幾個坑，都已修：

- **NSIS 本身是 32 位元程式**，所以登錄檔預設寫進 `WOW6432Node`（64 位元程式不會去那裡找），捷徑預設進「安裝者自己的」開始功能表而不是全機。用系統管理員裝完、換一般使用者登入就看不到捷徑 —— 加 `SetRegView 64` 與 `SetShellVarContext all` 才對。
- **`.nsi` 檔必須存成 UTF-8 with BOM**，否則 Unicode 模式下中文字串會編譯失敗。
- **`UninstallString` 本身已經含引號**，再包一層會變成 `""C:\…\uninstall.exe""`，啟動失敗而 `ExecWait` 不會回報錯誤 —— 升級於是靜靜地退化成覆蓋安裝。改用 `InstallLocation` 自己組路徑，並檢查 `ExecWait` 的結束碼。

不需要單一檔案的話，`build-full\bin\rpa-studio.exe` 本身就能直接跑，所有 DLL（Qt、OpenCV、ONNX Runtime）都已部署在旁邊。也可以直接把流程檔當參數丟給它：

```
rpa-studio.exe my-flow.rpa.json
```

## 建置

需要 **MSVC 2022**（或 VS 2022 Build Tools）、**CMake ≥ 3.24**、**Ninja** —— 後兩者 Build Tools 已內附，`build.cmd` 會自動找到。

### 最小建置（零外部 SDK）

```
build.cmd
```

只建 `rpa-core` + `rpa-server` + `rpa-recorder` + `rpa-cli` + 測試。不需要 Qt、OpenCV 或 ONNX Runtime，所以一份乾淨 checkout 就能建置並跑過全部測試。

### 完整建置（含 GUI 與視覺模組）

```
build-full.cmd
```

預期相依位於 `fetch-deps.ps1` 放置的位置（`C:\Qt\6.8.3\msvc2022_64`、`C:\deps\opencv\build`、`C:\deps\ort\...`）。若你的相依在別處，直接用 `build.cmd` 加參數：

```
build.cmd -DRPA_BUILD_VISION=ON -DRPA_BUILD_STUDIO=ON ^
          -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64 ^
          -DOpenCV_DIR=C:\opencv\build ^
          -DONNXRUNTIME_INCLUDE_DIR=C:\ort\include ^
          -DONNXRUNTIME_LIBRARY=C:\ort\lib\onnxruntime.lib
```

| 選項 | 預設 | 說明 |
|---|---|---|
| `RPA_BUILD_TESTS` | ON | 單元＋整合測試 |
| `RPA_BUILD_CLI` | ON | 無頭工具 |
| `RPA_BUILD_SERVER` | ON | REST API |
| `RPA_BUILD_RECORDER` | ON | 錄製器（Windows only） |
| `RPA_BUILD_VISION` | OFF | 需要 OpenCV + ONNX Runtime |
| `RPA_BUILD_STUDIO` | OFF | 需要 Qt 6；會自動開啟 VISION 與 SERVER |

視覺與 GUI 模組預設關閉，是為了讓一份乾淨的 checkout 在沒裝 SDK 的情況下也能建置並跑過測試。

### 取得 Qt 6 / OpenCV / ONNX Runtime

以 vcpkg 為例：

```
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
C:\vcpkg\vcpkg install opencv4[core] onnxruntime qtbase --triplet x64-windows
build.cmd -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake ^
          -DRPA_BUILD_VISION=ON -DRPA_BUILD_STUDIO=ON
```

Qt 也可用官方 online installer，然後傳 `-DCMAKE_PREFIX_PATH=C:\Qt\6.x.y\msvc2022_64`。

## 測試

```
ctest --test-dir build --output-on-failure
```

141 項測試，分兩組：

- `rpa-tests` — IR 往返、驗證規則、變數展開、按鍵解析、Executor 控制流（含 `on_fail` 的 abort/continue/goto、迴圈上限、停止 latch）、**AI `output_schema` 契約**（下述）、**SSE 分幀與回應解碼**（含一份真實 gateway 回應的重播）、**草稿修正迴圈的收斂條件**，以及**真正啟動 HTTP listener** 對 REST API 發請求的整合測試。
- `example-flows-validate` — 用 `rpa-cli validate` 把 `examples/*.rpa.json` 全部過一次解析器，避免範例腐爛。

`output_schema` 那組測試值得特別說明：它把 schema 與 IR 綁在一起雙向檢查 —— schema 宣告的每個 step type 必須能被 IR 解析器接受，而 IR 支援的每個 step type 也必須出現在 schema 與 system prompt 裡。少一邊就代表「AI 能產出但客戶端只能拒絕」或「AI 永遠不會用到的步驟」，這種漂移在真實請求裡很難察覺。

### 各模組目前的驗證程度

全部模組都已編譯（零警告），完整建置也已打包成 exe 並實際啟動驗證。但「能編譯、能啟動」和「功能經過驗證」是兩件事：

| 模組 | 驗證程度 |
|---|---|
| `rpa-core` | 編譯 + 單元測試（IR、驗證、變數、Executor 控制流） |
| `rpa-ai`（schema／prompt） | 編譯 + 契約測試；刻意與 Qt 解耦成 `rpa-ai-schema` 目標就是為了能測 |
| `rpa-server` | 編譯 + 整合測試（實際 listener、認證、409 忙碌、發佈／取消發佈） |
| `rpa-cli` | 編譯 + 實測 `validate` 與 `serve`（真實 HTTP 請求） |
| `rpa-studio` | 編譯 + 實測啟動、積木畫布渲染（含兩層 C 型嵌套）、面板→積木→設定的完整路徑 |
| `rpa-studio`（目標選取器） | 編譯 + **在真實混合 DPI 雙螢幕上量測凍結畫面對齊到 0px 偏移** |
| `rpa-studio`（ngrok 通道） | 編譯 + **實測整條路徑**：公開網址打到 API、`--url` 固定網域、關閉時連轉接檔的子行程一併終止 |
| `rpa-studio`（Cloudflare 通道） | 編譯；**本機未安裝 cloudflared，參數組裝與 `Registered tunnel connection` 判定未對真實 agent 驗證** |
| 相對定位（三級） | 編譯 + **三級全部對真實視窗實測**：UIA 比對名稱、UIA 幾何推算、視覺後端；含歧義錨點與四種失敗訊息 |
| 相對定位（選取器提議） | 編譯；**提議面板未實際以 GUI 操作驗證**（需對桌面送合成事件） |
| `rpa-pack` | 編譯 + 實測冷啟動、熱快取、半毀快取復原 |
| `rpa-vision`（OCR） | 編譯 + **對已知答案的圖與真實 UI 截圖實測整條管線**（詳下） |
| `rpa-vision`（截圖） | 編譯 + 螢幕擷取與多螢幕列舉實測 |
| `rpa-recorder` | 編譯；**hook 與 UIA 未在實機錄製情境驗證** |
| `rpa-ai`（`AgentClient`） | 編譯 + **對真實 gateway 實測整條串流**（詳下），錄下的回應成為離線 fixture |
| `rpa-studio`（失敗截圖） | 編譯 + **實測 API 觸發的失敗流程確實產出截圖並回報路徑** |

另外**積木的拖曳重排我沒有實測**。驗證它需要對桌面送出合成滑鼠事件，而我試過一次就因為視窗沒真的取得前景，點擊落到了別的應用程式上 —— 這種測法在別人的桌面上風險太高，所以我停手了。拖曳的邏輯（子樹整體搬移、插入點計算、拒絕把積木丟進自己內部）是寫好並逐行檢查過的，但請你自己拖幾下確認手感。

### OCR 管線實測時抓到的兩個 bug

把模型內建進來以後才第一次真的跑通這條管線，而它一開始一行字都讀不出來。兩個問題都是「不會報錯，只會安靜地產出垃圾」那一類：

1. **裁切圖被旋轉 180 度。** `cropRotated` 依賴 `cv::RotatedRect::points()` 的角點順序，但那個順序**隨矩形角度轉動** —— `minAreaRect` 對同一條水平文字行，可能描述成 `(寬, 高, 0°)` 也可能是 `(高, 寬, 90°)`，取決於輪廓。用固定索引取角就會 warp 出轉了 90 度的圖；接著它是縱向的，又觸發「縱向文字轉 90 度」的啟發式，總共 180 度。辨識器拿到顛倒的字只會回報低信心值，沒有任何地方指向真正的原因。現在角點順序改成從幾何本身推導（依 x 分左右、各自依 y 分上下）。
2. **空格永遠讀不出來。** PaddleOCR 在 `use_space_char` 下會在字元集尾端追加一個空白，但**不會寫進字典檔**。所以模型的類別數是字典行數 + 2，而那個空白類別的索引落在字典範圍外，被邊界檢查靜默丟掉。「Sign in to continue」會變成「Signintocontinue」。現在載入時會對帳類別數：差 2 就補上空白，其他數字就**拒絕載入並說明差多少**，因為不成對的字典照樣能解碼成有信心的亂碼。

### AI gateway 實測時抓到的三個問題

SSE 解析原本沒對真實服務跑過。接上去之後：

1. **逾時被回報成「gateway 沒回結果」。** 判斷用的是「有沒有收到 `values` 事件」，但 gateway 在跑的途中就會送出不含 `structured_output` 的中間快照 —— 所以一個被逾時切斷的串流「有東西」，於是錯誤訊息說服務沒回應，把人指向 gateway，而真正的原因是逾時。現在改看**有沒有收到帶結果的快照**。
2. **中斷的串流仍掛著 `HTTP 200`。** 那是 header 的狀態碼，串流是後來才斷的。「HTTP 200: Operation canceled」讀起來像成功。現在只有狀態碼本身就是問題（≥400）時才報它。
3. **驗證腳本自己把中文送壞了。** Windows PowerShell 5.1 讀無 BOM 的 `.ps1` 用 ANSI，腳本裡的中文提示在執行前就變成亂碼 —— 而這**不會失敗**：AI 照樣回一份完整流程，只是回答的是別的問題（要求「輸入哈囉」，產出「輸入已成功登入」）。是比對回覆與要求才發現的。腳本現在存成 UTF-8 with BOM，`rpa-ai-probe` 也支援 `--message-file` 走檔案避開 shell 編碼。

驗證方式可重跑：

```
powershell -ExecutionPolicy Bypass -File tools\verify-agent-gateway.ps1
```

它依序測連線、產流程、改流程、錯金鑰、錯網址、逾時六種情況，並把真實回應存成 `--dump-sse` 檔。金鑰取自 `RPA_AI_API_KEY` 或桌面程式存在 Credential Manager 的那把，不會印出來。

`tests/data/gateway-stream.sse` 就是這樣錄下來的一份真實回應，測試會**逐 64 位元組**餵給解析器重播，確認還原出同一份流程 —— 包含中文 `哈囉` 完整穿過請求編碼、AI 巢狀的 `params_json`、以及客戶端的二次解析。這條路徑上沒有任何一關會對編碼錯誤報錯：一個打出亂碼的流程照樣跑得很順。

還沒對真實資料驗證的剩這一項：

- **錄製器的 hook 與 UIA** —— 未在實機錄製情境驗證。

## AI 助手

用自然語言描述要做的事，AI 回一份流程草稿。草稿**不會直接寫進流程** —— 它停在聊天面板，按「預覽」看差異、按「套用」才生效。

### 草稿不合格式時，AI 自己修

AI 產出的步驟參數包在 `params_json` 字串裡，客戶端收到後會用 IR 重新驗證一次。以前驗證不過的步驟只會被列出來，要人去改 JSON —— 但**這個工具的使用者不會讀 JSON**，列給他們看等於是死路。

現在客戶端會把錯誤清單連同 AI 自己寫的那幾行原文送回去要求修正，最多兩次，過程中聊天室顯示「草稿有 N 個積木不合格式，正在請 AI 修正…」。兩件事會讓它停下來：

- 到達次數上限
- **連續兩次的錯誤完全相同** —— 代表 AI 沒在收斂，再送一次只是多花一次錢

修不好才把問題列給使用者。每輪的花費會累加後一起顯示，不會只報最後一次的價錢。

### 執行失敗時，問 AI 哪裡出錯

流程失敗時，執行紀錄面板的「請 AI 看看哪裡出錯」會亮起來。它把失敗的步驟、錯誤訊息、整份流程、日誌尾段，加上**失敗當下的螢幕截圖**送給 AI，回來的修正草稿走一樣的預覽／套用閘門。

截圖在流程失敗的當下就拍好存在專案資料夾的 `runs/`，但**只有按下那個按鈕才會送出** —— 按鈕本身就是同意。這不是形式：截圖是整個桌面，實測時 OCR 就從畫面上讀到了旁邊終端機裡的 API 金鑰。所以它預設留在本機，REST API 也只回報路徑、不提供下載。

拍照的時機是 `run()` 返回的當下，不是每個步驟失敗時 —— `on_fail` 設成 continue 的步驟失敗後流程還會繼續，對那些拍照只會存一堆最終成功的流程的截圖。

## OCR

**模型內建在 exe 裡，不用下載、不用設定。** 首次啟動解壓後，`models/` 就在執行檔旁邊，程式自動找到它。

用的是 **PP-OCRv5 mobile**（偵測 4.6 MB + 辨識 15.8 MB + 18,383 字的字典）。選它而不是更小的 PP-OCRv4 中文模型，是因為 v4 的字集 `ppocr_keys_v1.txt` 只有 6,623 字、以簡體為主，**缺了這個程式自己介面在用的字**。拿本程式的截圖實測，同一張圖：

| 正確 | PP-OCRv4 | PP-OCRv5 |
|---|---|---|
| 刪除**積**木 | `刪除猜木` ✗ | `刪除積木` ✓ |
| 中止整**個**流程 | `整固流程` ✗ | `整個流程` ✓ |
| **描**述你想要的流程 | `苗述…` ✗ | `描述…` ✓ |
| 這個積木是做什麼的 | 未讀出 | ✓ |

「積」根本不在 v4 的字典裡，所以那不是辨識錯誤，是它連輸出的選項都沒有。v5 用簡繁日英統一字集，才是繁中該用的。

模型是 Apache-2.0，來源與授權寫在 `models/NOTICE.txt`。官方只發佈 Paddle 格式，這裡放的是社群的 ONNX 轉檔版；`NOTICE.txt` 也寫了怎麼自己用 `paddle2onnx` 轉。

### 用指令檢查 OCR

不必開 GUI 就能測整條管線：

```
rpa-cli ocr screenshot.png
rpa-cli ocr screenshot.png --dump-crops out\   # 連餵給辨識器的裁切圖一起倒出來
```

它會印出每一行的文字、框、信心值，以及**分階段的統計**：偵測到幾個框、幾個裁切失敗、幾個信心不足被丟掉。這很要緊，因為管線每一關都是靜默丟棄候選，只看「找不到文字」無法判斷是偵測沒看到、還是辨識讀不出來。

### 換成別的模型

設定頁「視覺引擎 → OCR 模型資料夾」指向一個含 `det.onnx` / `rec.onnx` / `keys.txt` 的資料夾即可，設定過的資料夾優先於內建。

字典必須與辨識模型成對。載入時會拿模型的類別數和字典大小對帳，不合就**拒絕載入並說明差多少** —— 因為不成對的組合照樣能解碼，只是解出看起來很有信心的亂碼，那比直接失敗難查得多。

沒有 OCR 也能運作：影像模板（`image_find`）不需要它。

## 多螢幕與 DPI 縮放

目標選取器同時活在兩個座標系裡，而它們在縮放螢幕上並不相等：

- **Qt 的邏輯像素** —— 視窗幾何與滑鼠事件用這個
- **物理螢幕像素** —— 螢幕擷取與 Executor 的點擊用這個

一台 2560×1440 的螢幕在 150% 縮放下，Qt 說它是 1707×960，Windows 擷取到的卻是 2560×1440。混合 DPI 桌面沒有單一比例可用：本機外接 1920×1080（100%）加筆電面板（150%），整個桌面的平均比例是 1.235×1.333，**對兩個螢幕都不對**。

所以選取器是**每個螢幕一個覆蓋視窗**，而不是一個橫跨桌面的視窗。每個視窗因此帶有它所在螢幕的 devicePixelRatio，凍結畫面就能 1:1 疊在它所拍攝的內容上，而框選出來的座標也用該螢幕自己的比例換算。單一視窗做不到這件事 —— Qt 只給一個視窗一個 DPR。

座標對不上時先跑診斷：

```
rpa-studio.exe --dump-geometry geom.txt
```

它會列出 Qt 看到的每個螢幕（含 DPR）、Windows 回報的物理矩形、實際採用的逐螢幕映射，以及那個「對誰都不對」的桌面平均值以供對照。另外 `--pick-target` 會直接開啟選取器，方便單獨測試擷取與 OCR，不必先組流程。

## 看一支程式暴露了什麼給自動化

Windows 程式的控制項樹從截圖上看不出來，而照猜測寫出來的定位只會在寫的人那台機器上動。先傾印出來看：

```
rpa-studio.exe --dump-uia tree.txt --uia-window 客戶資料設定
rpa-studio.exe --dump-uia tree.txt --uia-delay 5      # 五秒內把目標視窗切到最前面
```

輸出是縮排的控制項樹，每個節點帶型別、名稱、AutomationId、類別、邊界，最後附上各型別的數量統計 —— 跟 OCR 回報分階段計數是同一個理由：定位失敗時，第一個問題永遠是「那支程式到底有沒有這種控制項」。

**檔案是 UTF-8。** PowerShell 的 `Get-Content` 預設不是，中文會變亂碼，要加 `-Encoding UTF8`。

實測 Windows 的「日期和時間」對話框，關鍵的是這兩行：

```
Text  name="日期:"  class="Static"  bounds=(336,257,250,15)
Edit  name="日期:"  id="118"  class="Edit"  focusable  bounds=(334,275,252,38)
```

**輸入框自己的名稱就是它旁邊那個標籤的文字。** Windows 會把緊鄰的 Static 自動掛成 Edit 的無障礙名稱，所以「找『日期』旁邊的輸入框」根本不必算座標 —— 直接找名稱是「日期」的 `Edit` 就好。這比 OCR 加方位推算穩定得多，也完全不受字級、DPI、版面位移影響。

同一份輸出裡 `labelled-by=` 全是空的，所以要靠 `name`，不要靠 `LabeledBy`。**但這是標準 Win32 對話框的行為，不保證每支程式都這樣** —— 所以定位是分兩級退化的，見下節。

## 相對定位：「某某旁邊的那個輸入框」

`{{變數}}` 之外，`click` 的目標可以用 `relative`，指定「某個標籤的哪一邊的什麼控制項」：

```json
{ "kind": "relative", "text": "客戶全稱", "match": "contains",
  "direction": "right", "element": "input", "max_distance": 400 }
```

`direction` 是 `right` / `left` / `above` / `below`，`element` 是 `input` / `button` / `checkbox` / `any`。

這解決的是**絕對像素偏移的脆弱**：`offset_y: 40` 在字級、DPI、版面一變就指到別的地方，而且不會報錯，只會點錯位置。

### 兩級退化，使用者不必知道自己面對哪一種

不同框架暴露給自動化的東西差很多，所以解析分兩步：

1. **比對控制項自己的名稱** —— 標準 Win32 表單會把緊鄰的 Static 自動掛成 Edit 的名稱，這時「客戶全稱旁邊的框」就是「名稱叫客戶全稱的 Edit」。**完全不用座標**，所以字級、DPI、版面位移都動不了它。

   **只在剛好一個控制項符合時才算數。** `contains` 比對很容易同時命中好幾個（「時區」就包含在「變更時區(Z)...」裡），而這一級沒有方位概念可以分辨它們 —— 所以多於一個就往下一級走，由那邊用方位決定。取樹狀順序的第一個等於用控制項階層的偶然性在挑，那是會安靜給出錯誤答案的做法。
2. **比對標籤的相對位置** —— 控制項沒掛名稱時，先找到標籤元素，再取指定方位上最近、且在垂直（或水平）方向與標籤有重疊的控制項。要求重疊是關鍵：不然隔壁列的欄位可能因為直線距離較近而被選走。

用了哪一級會寫進執行紀錄。這件事值得看：一個原本靠名稱命中的步驟開始改用幾何推算，它還會動，但離壞掉只差一次版面調整。

### 先測再放進流程

```
rpa-studio.exe --probe-relative 客戶全稱 --uia-direction right --uia-element input --uia-window 客戶資料設定
```

只印結果、**不會點下去**。實測 Windows「日期和時間」對話框：

```
--- 日期: / below / input
found   Edit  name="日期:"
via     by-name (the control carries the label)
bounds  (334,275,252,38)
click   (460,294)   -- not clicked

--- 日期: / below / button          ← 名稱對不上，退到第二級
found   Button  name="變更日期和時間(D)..."
via     by-geometry (nearest in that direction)
```

找不到時會指出是哪一種失敗，因為要改的地方完全不同：

```
not found: UI Automation found no element named '不存在的標籤' in 23 controls
not found: 2 controls match '變更' and none of them sits right of a label with
           that text -- make the anchor more specific
```

四種：標籤根本不在、錨點太籠統同時命中多個、標籤在但那種控制項這支程式沒暴露、標籤在但那個方向的範圍內沒東西。

> studio 是 GUI 子系統程式，直接在 PowerShell 執行看不到輸出，要用 `-RedirectStandardOutput` 導到檔案再讀（記得 `-Encoding UTF8`）。

### 第三級：UIA 看不到的畫面

遠端桌面、Citrix、canvas 自繪的介面對自動化是一片空白 —— 看得到應用程式，但沒有應用程式可問。這時由視覺後端接手：OCR 找錨點，再用邊緣偵測在指定方位找方框。

門檻刻意調低（Canny 20/60，而不是常見的 100/200）：現代扁平介面的輸入框外框只比背景深一階，用一般門檻會完全看不到。找到的方框會排除「把錨點整個包住的那個」—— 那是背後的面板，不是欄位。

信心值會乘上 0.8。錨點是讀到的、欄位是推論的，比較兩種結果時應該看得出這是比較弱的那條路。

實測（UIA 對 VS Code 那棵樹答不出來時）：

```
--- 'EXPLORER' / below / any
found   "EXPLORER"
via     vision (OCR anchor + box detection)
click   (2120,145)   -- not clicked
```

**完全沒有邊框、跟背景同色的欄位這樣找不到**，錯誤訊息會直說並建議改用圖片模板。

### 探針走的是同一條串接

`--probe-relative` 不指定 `--uia-window` 時，跑的是**執行流程時完全相同的 CompositeLocator**，所以印出來的就是那個步驟會做的事，包括退到哪一級。兩邊都失敗時，兩個原因都會列出來：

```
not found: UI Automation found no element named 'CODEX' in 478 controls;
           no OCR line matched the anchor 'CODEX' (107 lines read).
           Closest: 'CLAUDE CODE', 'CODEY', '> Vs CODE POKÉMON'
```

「這支程式沒暴露那個控制項」和「那行字 OCR 根本沒讀對」是兩件事，只留最後一個會把先發生的那個藏起來。

## 無頭使用

```
rpa-cli validate flow.rpa.json          # CI 用；有任何驗證問題就回非零
rpa-cli show flow.rpa.json              # 正規化重新輸出
rpa-cli run flow.rpa.json password=xxx  # 實際執行，覆寫變數
rpa-cli serve ./published --port 8420 --key sk-pra-...
```

`rpa-cli` 不連結 `rpa-vision`，所以 `ocr_find` / `image_find` 步驟會回報「vision module not available」並讓該步驟失敗。要無頭跑帶錨點的流程，請用 `rpa-studio` 的 API server。

## REST API

```
GET    /api/v1/health                  # 免認證
GET    /api/v1/scripts
POST   /api/v1/scripts?id=<id>         # body = 流程 JSON 本身；id 是查詢參數
GET    /api/v1/scripts/{id}
DELETE /api/v1/scripts/{id}
POST   /api/v1/scripts/{id}/run        # 202 + run_id + queue_position
GET    /api/v1/runs/{id}
GET    /api/v1/runs?limit=&offset=
DELETE /api/v1/runs/{id}               # 取消還在排隊的
GET    /api/v1/queue                   # 目前執行中 + 等待中
```

除 `/health` 外都需要 `X-API-Key`。**未設定任何 key 時，所有認證端點一律回 401**——這比預設開放安全。

### 啟動伺服器

兩條路徑，差別在於**有沒有視覺定位**：

| | 啟動方式 | `ocr_find` / `image_find` |
|---|---|---|
| 桌面程式 | 工具 → API 伺服器 → 啟動 | ✅ 可用 |
| 無頭 CLI | `rpa-cli serve <資料夾> --port 8420 --key <金鑰>` | ❌ 用 `NullTargetLocator`，該步驟必定失敗 |

CLI 沒有連結 `rpa-vision`，所以要跑帶文字或圖片錨點的流程，必須用桌面程式那條。

### 觸發一次執行

```bash
curl -X POST http://127.0.0.1:8420/api/v1/scripts/erp-login/run \
  -H "X-API-Key: sk-pra-..." -H "Content-Type: application/json" \
  -d '{"variables":{"password":"..."}}'
# 202 {"run_id":"run_97a5a8df","script_id":"erp-login"}
```

**是非同步的**：`/run` 立刻回 202 和 `run_id`，不等流程跑完。結果要輪詢：

```bash
curl http://127.0.0.1:8420/api/v1/runs/run_97a5a8df -H "X-API-Key: sk-pra-..."
# {"status":"succeeded","steps_executed":2,"started_at":...,"finished_at":...}
```

`status` 會是 `queued` / `running` / `succeeded` / `failed` / `cancelled`。

流程失敗時會多一個 `failure_screenshot`，指向失敗當下的畫面：

```json
{ "status": "failed", "failed_step": "look",
  "error": "no OCR line matched '確認送出' (156 lines read). Closest: ...",
  "failure_screenshot": "C:/Users/user/Documents/RPA-Block/runs/run_1f7b88b9-failed.png" }
```

那是**執行那台機器上的路徑，不是下載連結** —— 截圖是整個桌面，可能拍到與流程無關的東西，所以它留在本機讓管理者去看，不經 API 傳出去。

### 排隊

桌面只有一組滑鼠鍵盤，所以流程一次只跑一個 —— 但**不需要呼叫端自己排隊**。同時打進來的請求會排成 FIFO，依序執行：

```
#1  run_601dde1b  queue_position=0     ← 立刻開始
#2  run_c11fe838  queue_position=1
#3  run_6fcfa5f5  queue_position=2
```

`queue_position` 是前面還有幾個；0 代表馬上跑。查目前佇列：

```bash
curl http://127.0.0.1:8420/api/v1/queue -H "X-API-Key: sk-pra-..."
# {"data":[{"run_id":"run_601dde1b","position":0,"state":"running"},
#          {"run_id":"run_c11fe838","position":1,"state":"waiting"}],"depth":2}
```

還沒開始的可以取消（已經在跑的不行 —— 那是執行器的職責，用 `F12` 緊急中止）：

```bash
curl -X DELETE http://127.0.0.1:8420/api/v1/runs/run_562d5ee9 -H "X-API-Key: sk-pra-..."
```

行為細節：

| 情況 | 回應 |
|---|---|
| 正常排入 | `202` + `queue_position` |
| 佇列滿（預設 32） | `429` |
| 等太久（預設 10 分鐘） | 該 run 標成 `cancelled` |
| 伺服器關閉時仍在排隊 | 該 run 標成 `cancelled` |
| `rejectWhenBusy = true` | 回到舊行為，忙碌時立刻 `409` |

**佇列有上限是刻意的。** 每個排隊的流程最後都會搶走滑鼠鍵盤，無上限的佇列等於把這台機器交出去好幾個小時，所以滿了就 `429` 讓呼叫端知道。同理，等待也有逾時：使用者在 UI 手動跑一個長流程時，API 進來的 run 會等它 —— 但不會永遠等下去，否則呼叫端只是在輪詢一個永遠不會開始的東西。

**UI 手動執行的流程不歸佇列管**，但佇列看得到桌面忙碌：被拒絕的 run 會留在隊首每 250ms 重試，直到桌面空出來或逾時。

### 流程的輸入參數

積木的文字欄位裡寫 `{{變數名}}`，執行時會換成實際值。變數在「編輯 → 流程變數…」（或工具列的「變數」）宣告，每個帶一個預設值。

值有三個來源，後面覆蓋前面：

1. **流程檔裡的預設值** —— 跟著檔案走
2. **REST API 的 `variables`** —— 每次呼叫帶進來
3. **UI 執行前的提示** —— 流程有宣告變數時，按「執行」會先讓你確認這次要用的值

**密碼不要寫進預設值。** 預設值會跟著流程檔散佈，也會被 API 公告出去。留空，改用上面第 2 或第 3 種方式逐次帶入。

呼叫端可以直接問這支流程吃什麼：

```bash
curl http://127.0.0.1:8420/api/v1/scripts -H "X-API-Key: sk-pra-..."
```
```json
{ "id": "order-lookup",
  "variables": { "order_id": "SO-0000", "operator": "未指定" } }
```

`variables` 一定存在，沒有參數時是空物件 —— 呼叫端不必分辨「這支不吃參數」和「這台伺服器不會講」。

```bash
curl -X POST http://127.0.0.1:8420/api/v1/scripts/order-lookup/run \
  -H "X-API-Key: sk-pra-..." -H "Content-Type: application/json" \
  -d '{"variables":{"order_id":"SO-2026-0042","operator":"王小明"}}'
```

### 複製可直接執行的指令

API 面板選一個已發佈的流程，按「複製 PowerShell」或「複製 curl（cmd／bash）」。指令會**帶入該流程宣告的參數與預設值**：

```
-d "{\"variables\":{\"url\":\"youtube.com\"}}"
```

沒有預設值的參數會列在指令下方提醒你填。預設值在「編輯 → 流程變數…」設定，設好**要重新發佈**才會反映到這裡 —— 發佈存的是快照。

### 流程的 id 從哪來

`POST /api/v1/scripts/{id}/run` 的 `{id}` 是**發佈時決定的識別碼**，跟流程的顯示名稱是兩回事。查有哪些可以觸發：

```bash
curl http://127.0.0.1:8420/api/v1/scripts -H "X-API-Key: sk-pra-..."
```
```
id（放進網址）           name（只給人看）
customer-statement       客戶對帳單
erp-login                ERP 登入
```

id 的來源，優先序由高到低：

1. **發佈時自己指定** —— 桌面程式「檔案 → 發佈到 API」會跳出對話框讓你填；HTTP 則用 `?id=`
2. **從名稱自動推導** —— 轉小寫、非英數換成 `-`
3. **從檔名推導** —— 用 `rpa-cli serve <資料夾>` 時

**中文名稱請自己指定 id。** 自動推導會丟掉非 ASCII 字元（id 必須同時能安全用在網址和檔名裡），所以「客戶對帳單」推不出任何可讀的東西，只會得到 `flow-fd87b175` 這種加了雜湊的識別碼 —— 唯一且穩定，但沒人想打進 webhook。指定 `customer-statement` 之類的會好用得多。

> 那串雜湊是後來補的。原本非 ASCII 名稱一律推導成 `flow`，所以**發佈第二個中文流程會直接覆蓋掉第一個**，而且不會有任何提示。

### 發佈流程

body 直接是流程 JSON，id 走查詢參數：

```bash
curl -X POST "http://127.0.0.1:8420/api/v1/scripts?id=erp-login" \
  -H "X-API-Key: sk-pra-..." -H "Content-Type: application/json" \
  --data-binary @erp-login.rpa.json
```

**驗證不過的流程會被拒絕（400）**，回應中的 `issues` 會列出原因。`parseScript()` 本身是寬鬆的——語意錯誤只記在 `issues` 裡仍然回傳流程，這對編輯器是對的（半成品也該畫得出來），但對這個端點是錯的：已發佈的流程由機器在無人看管下觸發，收下一個跑不動的流程只是把失敗延後到沒人會看見的地方。

預設只綁 `127.0.0.1`。綁 `0.0.0.0` 等於把這台機器的滑鼠鍵盤開放給網路上任何持有 key 的人，UI 會要求明確確認。

### 從網際網路觸發（對外通道）

要讓公司外的系統觸發流程，又不想動路由器和防火牆，API 面板有「對外通道」，可選 **ngrok** 或 **Cloudflare Tunnel**。已發佈流程的「複製 curl」在通道開著時會自動改用公開網址。

**兩種 agent 都沒有包在安裝檔裡，要自己安裝。** 這不是偷懶 —— ngrok 的條款只允許在「由我們、而不是使用者持有帳號」的前提下轉散布 agent，而這裡的設計正是使用者用自己的 token。面板上的「取得 …」按鈕會開啟對應的下載頁。

**兩種都必須填 token，沒填不給開。**

|  | ngrok | Cloudflare Tunnel |
|---|---|---|
| token | 帳號的 authtoken | Zero Trust 後台那條通道的 `eyJ…` |
| 傳遞方式 | `NGROK_AUTHTOKEN` 環境變數 | `TUNNEL_TOKEN` 環境變數 |
| 固定網址 | 「固定網域」欄位（`--url`），需先在後台保留 | 後台設定的公開主機名稱（必填） |
| 本機位址 | 由本程式指定 `127.0.0.1:<port>` | **在 Cloudflare 後台設定**，本程式改不了 |

token 一律走環境變數而非命令列參數 —— 命令列在工作管理員裡是看得到的，而任一 token 都足以在使用者的帳號上開通道。兩者都存在 Windows 認證管理員，**而且分開存**，切換服務不會弄丟另一組。

通道只連到 `127.0.0.1:<port>`，所以開通道**不需要**同時把伺服器綁到 `0.0.0.0`，暴露面比後者小。

**這是整個程式裡最危險的開關。** 通道直接穿過防火牆，之後擋在網際網路和這台電腦的滑鼠鍵盤之間的，只剩那把 API 金鑰。所以按下去會跳出一個講白話的確認框，而且沒有 API 金鑰時不讓開。開著時主視窗狀態列會顯示紅色的「API：對外開放中」，關掉 API 面板也還看得到。

#### Cloudflare 的那個坑

token 模式的 Cloudflare 通道**沒有 `--url` 可以指定本機服務位址** —— 那是在後台的「公開主機名稱」裡設定的。如果後台沒指向 `http://127.0.0.1:<port>`，通道會連上、面板會顯示已開放，但**什麼都不通，而且沒有任何地方會報錯**。

所以通道連上後，程式會從公開網址對自己的 `/api/v1/health` 打一次，確認真的通得回來；不通就跳警告說明該去後台改什麼。這是唯一能當場抓到這個設定錯誤的辦法，否則要等到客戶半夜觸發失敗才會發現。

上一條通道沒關乾淨時，ngrok 會用 `ERR_NGROK_334` 拒絕下一條（實測撞到過：強制結束 agent 之後，ngrok 那邊還記著端點還在線上）。這個錯誤已經翻成「等十來秒再開一次」，不會只丟一個錯誤碼給使用者。

#### 長期掛在外網

要讓一台機器長期可以從外部觸發，勾選「程式啟動時自動開通道，斷線自動重連」。開啟後：

- 程式啟動、API 伺服器起來之後，通道自動跟著開
- agent 非預期結束時自動重連，間隔 5 → 15 → 30 → 60 → 120 秒後固定重試，連上就把間隔歸零
- **手動按「關閉通道」會連同待重連一起取消**，不會關掉之後又自己開回來

沒有 API 金鑰時不會自動開 —— 那種情況下所有需認證的端點本來就一律拒絕，開通道只是把機器公告出去而已。

**一定要設固定網址。** 自動重連能讓服務回來，但**解決不了網址會變**：ngrok 沒保留網域時每次重連都換一組，呼叫端手上那個就失效了。ngrok 要用付費方案保留網域填進「固定網域」欄位；Cloudflare 具名通道的主機名稱本來就是固定的。

還有兩件事跟長期掛著有關，跟這支程式無關但會咬人：

- **那台機器必須保持登入、螢幕不能鎖。** RPA 要有互動式桌面才送得出鍵盤滑鼠、才截得到畫面；螢幕一鎖，流程就會以「找不到目標」或「模板比對分數過低」的形式失敗。
- **失敗截圖會拍到整個桌面。** 一台長期無人看管的機器上，那些截圖會累積在 `runs/`，內容是當下畫面上的一切。

#### 認證失敗有速率限制

同一個視窗（60 秒）內累積超過 10 次認證失敗後，後續**失敗的**請求改回 `429` 並帶 `Retry-After`。

只計失敗、也只擋失敗：**持有正確金鑰的呼叫端永遠不受影響**。否則這就成了一個把流程擁有者自己鎖在門外的手段 —— 任何人只要一直亂猜就能讓正牌自動化停擺。因為通道後面的請求全都來自 `127.0.0.1`，這個計數是全域的，分來源計算只會是「一個桶假裝成很多桶」。

#### 關閉通道時必須連子行程一起關

套件管理員裝的 agent 往往不是 agent 本身。Chocolatey 裝出來的 `C:\ProgramData\chocolatey\bin\ngrok.exe` 是一個 **384 KB 的轉接檔**，它啟動真正的 46 MB 執行檔當子行程：

```
25368  C:\ProgramData\chocolatey\bin              ← 我們啟動的
14364  C:\ProgramData\chocolatey\lib\ngrok\tools  ← 真正在服務的
```

只終止我們啟動的那個，**通道會繼續開著，而介面會顯示「已關閉」** —— 這是這個功能最糟的失敗模式：使用者以為關了，機器其實還在網路上。所以 agent 是被放進 **Windows job object** 裡執行的，關閉時終止整個 job，連子行程一起帶走（實測：終止後存活數 0）。job 設了 `KILL_ON_JOB_CLOSE`，所以**程式當掉沒來得及收尾時，通道也會跟著死**，不會留下一台開著的機器。

## 交機給客戶：唯讀模式

裝在客戶那台的版本可以**執行與發佈既有流程，但不能建立或修改**。要編輯得輸入編輯密碼。

交機前在「檔案 → 鎖定編輯」按一下，之後：

| | 唯讀 | 解鎖後 |
|---|---|---|
| 執行、單步、停止 | ✅ | ✅ |
| 開啟既有流程 | ✅ | ✅ |
| API 伺服器、對外通道 | ✅ | ✅ |
| 新增／儲存／另存 | ❌ | ✅ |
| 錄製、AI 套用草稿、發佈 | ❌ | ✅ |
| 積木、積木設定、AI 助手三個面板 | 隱藏 | 顯示 |

面板是**隱藏**而不是變灰：一個按不下去的積木面板等於在廣告一個這份安裝沒有的功能，只會引人去找密碼。標題列會顯示「（唯讀）」。

密碼**不以明文存在任何地方** —— 編進執行檔的是加鹽的 SHA-256，實測 `sc9…` 那串明文不在 exe 裡（UTF-8 與 UTF-16 都查過）。

### 這道鎖擋得住什麼、擋不住什麼

擋得住：客戶的人員用這套工具**順手做自己的自動化**。這是它的目的，而且它有效。

擋不住的，你要知道：

- **流程檔是純文字。** 任何人都能用記事本寫一份 `.rpa.json`，再用 `rpa-cli run` 或 REST API 跑起來 —— 那條路完全不經過這個 GUI 的鎖。
- **拿得到執行檔的人可以改掉那個比對。** 這是本機驗證的固有限制，跟雜湊強度無關。
- **所有客戶共用同一組密碼**，所以一次外洩等於全部解開。

如果這件事在商業上真的重要，該做的是**把授權綁到每台機器**（例如以機器碼換一份簽章過的授權檔），而不是共用密碼。目前這個做法是一道門檻，不是一把鎖。

## 安全性須知

- **緊急中止**（預設 `F12`）註冊為全域快捷鍵。流程搶走指標時，這是唯一可靠的逃生門。
- **MSVC 執行期 DLL 會部署在執行檔旁邊。** `windeployqt` 加了 `--no-compiler-runtime`，所以沒有任何東西會帶上 `msvcp140.dll` 那幾支 —— 這在開發機上看不出來（裝了 Build Tools 就已經有），到客戶那台就是開機即缺 DLL，連自己的程式碼都還沒跑到。改由 CMake 的 `InstallRequiredSystemLibraries` 複製過去，兩種包裝都涵蓋。
- **API 金鑰用作業系統的亂數源產生**，不是用 PRNG。原本是 `mt19937` 拿單一個 `random_device` 值當種子 —— 金鑰看起來有 62³² 種可能，實際上只有 2³²，因為種子是唯一的未知數。這在只綁 loopback 時無所謂，一旦開了 `0.0.0.0` 或對外通道就完全不同：那把金鑰是網際網路和這台機器滑鼠鍵盤之間唯一的東西。
- AI gateway 的 secret 存在 **Windows Credential Manager**，不與其他設定一起放註冊表。
- 截圖只在使用者明示（聊天室的附截圖鈕、錄製）時才送出。
- 流程檔中的模板路徑相對於專案資料夾，所以流程可在機器間搬移；但**不要把密碼寫進流程**，改用 REST API 的 `variables` 逐次傳入。

## 專案結構

```
RPA-Block/
├─ fetch-deps.ps1         # 下載預編譯 Qt / OpenCV / ONNX Runtime + OCR 模型
├─ build.cmd              # 最小建置（無外部 SDK）
├─ build-full.cmd         # 完整建置（含 GUI 與視覺模組）
├─ package.ps1            # 打包成 dist/RPA-Block.exe
├─ CMakeLists.txt         # 模組開關與相依探測
├─ third_party/           # vendored: nlohmann/json、cpp-httplib
├─ docs/                  # 需求規格、畫面線框
├─ tools/                 # verify-agent-gateway.ps1（對真實 gateway 的實測腳本）
├─ examples/              # 可驗證的範例流程
├─ models/                # 內建的 PP-OCRv5 模型（打包進 exe；見 NOTICE.txt）
├─ rpa-core/              # IR + Executor + 邏輯↔物理座標映射（無外部相依）
├─ rpa-vision/            # 截圖、多螢幕列舉、OCR、模板比對
├─ rpa-recorder/          # 輸入 hook、UIA
├─ rpa-ai/                # agent 客戶端 + 可測的 schema 目標
├─ rpa-server/            # REST API
├─ rpa-studio/            # Qt 6 桌面應用（含 Theme、resources/ 圖示）
├─ rpa-cli/               # 無頭工具
├─ rpa-pack/              # 單一 exe 的自解壓縮 stub
└─ tests/                 # 單元 + 整合測試
```

`build-full/`、`dist/` 與相依目錄都在 `.gitignore` 內。執行期產生的資料（`recordings/`、`published/`、`assets/*.png`、`runs.jsonl`）都落在設定頁指定的**專案資料夾**，預設是 `Documents/RPA-Block`，不會污染原始碼樹。

## 已知限制

- 錄製器與輸入模擬是 Windows 專屬（`SendInput`、`SetWindowsHookEx`、UI Automation）。`rpa-core` 本身可跨平台編譯，但執行流程目前只在 Windows 有意義。
- 巢狀 `如果` / `重複` 的內容直接在畫布上拖放編輯；屬性面板裡的 JSON 只供檢視。
- 積木一律垂直堆疊，沒有自由擺放的座標概念 —— 流程是一條序列，不是圖。
- `screenshot` 步驟的 `region` 欄位 IR 支援、也會正確往返存檔，但屬性面板沒有編輯它的 UI（該面板的 offset／region／retry 區塊只對點擊與兩個 locate 步驟顯示）。
- Studio 需要 **Qt 6.3 以上**（`QWidget::addAction(text, shortcut, receiver, slot)` 這個 overload 從 6.3 才有）。
- AI 產出的巢狀步驟包在 `params_json` 字串內（`OutputSchemaV1` 不支援遞迴型別），客戶端收到後會再驗證一次；驗證失敗的步驟會送回給 AI 修正（上限兩次），仍修不好才逐一列出而非整份丟棄。
- 同一時間只允許一個流程執行（桌面獨占輸入）。UI 與 API 的競爭由 `ExecutionController` 的 busy latch 決定，後到者收 409。
- 框選範圍不能跨螢幕。兩邊需要不同的縮放比例，而跨越螢幕邊界的目標本來也不是 Executor 能點的東西。
- 圖片模板存的是擷取時那個螢幕的物理像素。把目標視窗搬到縮放比例不同的螢幕後，模板的實際尺寸就變了 —— 比對器有做多尺度搜尋來吸收這個差異，但若相似度一直不過，重新在該螢幕上截一張模板是最快的解法。
