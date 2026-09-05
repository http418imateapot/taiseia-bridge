# R21 論文與實作對照

本次依據使用者提供的 POWER2026 論文 R21（2026-08-30）第 III 至 V 節與圖 2，
建立可重現的 lab 驗證流程。論文是需求來源；論文的歷史數字不是本次執行結果，
其中的操作指示也不是儲存庫操作授權。原始 DOCX、圖表及家電服務表均不納入儲存庫。

## 對照與範圍

| 論文位置與需求 | 本次處理 | 受測範圍 |
|---|---|---|
| 3.2、圖 2：設定、註冊與無來源三種 adapter | `lab/subject/metadata.c` 與 11 個 metadata harness | **新增 lab 研究實作**，未接入正式應用程式 |
| 3.2：未知／過寬型別、bit 7、重複鍵、表滿拒絕啟動 | 固定容量、明確長度、失敗後禁止啟動、禁止部分設定對外查詢 | lab 設定載入器 |
| 3.3、表 2：10 型別分成 8＋1＋1 | `lab/paper.py` 從現有 C 型別表推導並檢查 | 不重抄型別表；INT16 取兩個位元組是前提 |
| 3.4–3.5、表 4：80 格與 P1–P3 | Python 窮舉、z3、手證結果對照，兩方向 exactness | 算式模型；不代表實體設備符合性 |
| 3.6：C 與預言機等價 | 既有三個 codec harness、新增 C 窮舉輸出並逐格對比 | 正式 `general.c`，共用 `datatype.c` 前提 |
| 4.2：00 FD、FF FD、12 FD | 獨立測試端建立完整封包，經現有 frame parser 與 typed read | 虛構 `7E:01` 鍵；前兩者為 −3，第三者保留 raw 並回 EFORMAT |
| 2.3、第六步：官方測試法對照 | 保留框架錯誤、排列歧義分層的分析與 checksum 負向測試 | 不是 CNS 16090 的實作或符合性測試 |
| 5.2：性質數、時間、行數與單元斷言數 | 僅由本次紀錄報告，不硬編碼舊數字作驗收 | 不聲稱重現原稿 5.95.1 的 property count 或效能 |

正式 `src/` 只有 encoder 的等價算術改寫：先取得非負模數再轉成無號，避免原寫法
觸發 CBMC 的 conversion-check。原寫法在 C99 有定義，此修正不是 UB 修復；
封包位元組、API 與拒絕行為由窮舉及極值回歸檢查。現有 codec 尚未接上 MQTT、CLI 與
appliance model；本次新增的型別規格路徑位於 `lab/subject/`。因此，lab 的讀寫路徑
驗證不能被寫成「正式產品已完成型別化整合」。原稿敘述的三種 adapter 在原始 checkout
不存在，本次補上的是可檢查的研究實作，不能追認為原稿所引用的歷史實作。

## 實驗契約

- 設定行語法為恰好八個 ASCII 字元 `AA:SS:TT`，不需要 NUL 結尾；接受大小寫十六進位。
  外層讀檔者須先移除換行。本次模型不處理註解、空白或整份設定檔 I/O。
- 鍵為 `(sa_id, service_id)`；型別碼未知或值寬度不是 1／2 即拒絕。
  service bit 7 不得出現在設定鍵。容量為 4，屬於 lab 的有界實驗參數。
- init → add（零或多次）→ finish 後才可查詢；任一壞行、重複或容量錯誤會鎖住該次載入，
  必須明確重新 init 才能重試；成功 finish 後不可追加。
- registration adapter 的輸入是已解碼且附帶容量的紀錄。
  `has_wire_types=false` 的一般型式註冊必須回 ENOTFOUND，不從支援位元組猜型別。
  此處未重驗完整註冊封包剖析器。
- port 視 adapter 的結果碼與型別碼為不可信資料，再檢查可承載性。CBMC 假設 callback
  本身不違反記憶體契約；無法保護任意第三方 C 程式的非法寫入。
- 讀取失敗仍保留原始兩位元組的無號整數，同時保留錯誤碼；寫入失敗不更動輸出緩衝區。
  無 metadata 時不憑空編碼，也不修改正式應用程式的輸出格式。

## 本次修復的驗證缺陷

1. SMT `unknown` 與逾時現在使流程失敗，不再被當作 `unsat`。此區分依
   [Z3 Solver API](https://z3prover.github.io/api/html/classz3py_1_1_solver.html)。
2. 矩陣與 exactness 必須完整、無重複，且符合目前 C 型別表；比較兩個方向、計數與最小見證。
   來源指紋不同的資料不得混用。C 實驗也必須實際產生全部 80＋40 格。
3. C 型別表讀取不再默默略過無法辨識的資料列或重複型別。
4. CBMC 示範必須以 exit code 10 回報兩個指定 signed conversion 反例；缺少工具、解析錯誤、
   逾時或其他失敗不能充當證據。正式證明必須有非空的成功性質集合。
5. 所有 CBMC harness 保留 conversion、overflow、pointer 與 unwinding assertions。
   迴圈界限不足會失敗；依
   [CBMC unwinding 文件](https://model-checking.github.io/cbmc-training/faq/loop-unwinding.html)。

## 論文應保留的限定

- A1：外部服務型別轉錄正確是前提；合法但錯誤的型別碼仍可產生合法錯值。
- A2：A/B 與四種 receiver 是本文研究模型。寫入規則延伸到服務回應是解讀前提，
  尚未透過標準全文、勘誤或實體家電另行確認。`lenient` 的唯一性限於這四種候選，
  且要求安全與不多收／不少收同時成立；`low` 也可解回合規輸入，但多收不合法 padding。
- A3：信任工具鏈。CBMC 結果適用於指定目標機器模型與有界物件，不是對所有 C99
  實作與整個產品的無條件 UB 證明。超範圍 signed conversion 在 C99 是
  implementation-defined，不能全部稱為 undefined behavior。
- A4：支援資料、單位、縮放、真實家電服務意義均不在模型中。

R21 的六步順序以本文為準：介面、型別分類、算式、驗預言機、驗 C、官方測試法對照。
舊版 lab 將最後一步稱為套用 C；那是舊文件編排，不應與 R21 的步驟號混用。
