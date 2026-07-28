
## upstreamとの差分

このフォークは [m5stack/StackChan](https://github.com/m5stack/StackChan)（`upstream/main`）を追従しており、現時点ではコミット `b72b3ed` から以下のように差分があります：

### 自動OTAアップデートの無効化

`CONFIG_OTA_URL`（`main/Kconfig.projbuild`）は、upstreamのxiaozhi-esp32用クラウドOTAサーバーを指したままになっていますが、このサーバーはStackChan向けのファームウェアイメージを配信していません。以前は起動時に`Application::CheckNewVersion()`がローカルのバージョンとサーバーが返すバージョンを比較し、新しそうであれば無条件に書き換えていたため、無関係／非互換なファームウェアが焼かれてしまう恐れがありました。この自動アップグレード処理は現在スキップされています（`patches/xiaozhi-esp32.patch`参照）。ただし、バージョン・アクティベーションの確認処理自体はmqtt/websocketの設定取得にも使われているため、そのまま動作しています。

### CO2センサー対応（M5Stack CO2L / Sensirion SCD41）

[M5Stack CO2Lユニット](https://docs.m5stack.com/en/unit/CO2L)（Grove **PORT.A**、SDA=G2, SCL=G1に接続）による音声アシスタント対応を追加：

- `main/hal/drivers/scd41/` — 最小限のSCD41用I2Cドライバ（低消費電力のシングルショット計測、CRC8検証あり）
- `main/hal/hal_co2.cpp` — HALラッパー。バッテリー消費を抑えるため、常時ポーリングではなくオンデマンドで計測（シングルショット、約5秒）を実行します。また10分ごとに計測を行うバックグラウンドタスクも動作し、CO2濃度が1000ppm以上（日本の建築物衛生法における換気基準）になった場合、画面上にアラートを表示し通知音を鳴らします。これは閾値を超えた瞬間に一度だけ発火し、濃度が下がるまで再アームされません。なお、クワイエットアワー（23:00〜07:00、`main/hal/hal_rtc.cpp`の`Hal::isQuietHours()`参照）中はアラートを抑制します
- `main/hal/hal_mcp.cpp` — `self.sensor.get_co2`というMCPツールを公開し、アシスタントがCO2濃度・気温・湿度についての質問に答えられるようにします
- `main/hal/board/stackchan.cc` — Grove PORT.A用に、PMICやタッチセンサーなどが使う内部I2Cバスとは別の、外部I2Cバスを追加

注記: この機能はGPIO2を再利用しています。GPIO2はupstreamでもレーザーポインター出力として`Hal::setLaserEnabled()`（`main/hal/hal_espnow.cpp`）に配線されていますが、この機体のハードウェア上では未使用です。この2つの機能は同時には使用できません。

### 環境センサー対応（M5Stack ENV Pro Unit / Bosch BME688）

[M5Stack ENV Pro Unit](https://docs.m5stack.com/en/unit/ENV%20Pro%20Unit)（Grove **PORT.A**）による音声アシスタント対応を追加：

- `main/hal/drivers/bme688/BME68x_SensorAPI/` — Bosch公式のBME68xセンサーAPI（vendored、BSD-3-Clause）。BMI270用IMUドライバと同じ方針で取り込んでいます
- `main/hal/drivers/bme688/` — センサーAPIの薄いI2Cラッパー。フォーストモード（オンデマンド）で気温・気圧・湿度・ガス抵抗値を取得します
- `main/hal/hal_env.cpp` — HALラッパー。オンデマンドでフォーストモード計測を行うほか、湿度とセンサーの生のガス抵抗値を自己調整型のベースラインと比較して算出する簡易的な「空気質スコア」（0〜100%、高いほど良好）、および屋内WBGT熱中症リスク指数の推定値を計算します
- `main/hal/hal_mcp.cpp` — `self.sensor.get_environment`というMCPツールを公開し、アシスタントが気温・気圧・湿度・空気質・熱中症リスクについての質問に答えられるようにします

注記: この実装では、較正済みのIAQ指数を得るためのBosch製BSECライブラリは**使用していません** — BSECはクローズドソースであり、そのライセンスは法人利用に限定され、コンパイル済みライブラリの再配布も禁止されているため、公開のホビープロジェクトのリポジトリとは相容れません。代わりに、よりシンプルなコミュニティ流のヒューリスティックで空気質スコアを算出しています。最初の計測値をベースラインとして、以降の問い合わせのたびに自己較正します（再起動でリセット）。そのため、これは認証された測定値ではなく、あくまで相対的・近似的な指標です。

ガスヒーター（およびそれに依存する空気質スコア）は、バッテリーでの消費電力を抑えるため現在**無効化**されています。BME688計測の中でガスヒーター（300℃のホットプレート）が消費電力の大部分を占め、他にはこのヒューリスティックにしか使われていないためです。気温・気圧・湿度・WBGTには影響しません。ヒーターを無効化した状態では、`gas_valid`は常にfalseとなり、`air_quality_percent`はツールのレスポンスから常に省略されます。必要であれば`main/hal/drivers/bme688/bme688.cpp`（`begin()`内の`heatr_conf.enable`）で再度有効化できます。

WBGT（暑さ指数、Wet-Bulb Globe Temperature）の推定値は、日本の環境省が定める屋内用の計算式 `WBGT = 0.7 × 湿球温度 + 0.3 × 黒球温度`（[wbgt.env.go.jp](https://www.wbgt.env.go.jp/)）に基づいています。湿球温度は気温・湿度からStull（2011年）の経験式で近似し、黒球温度は（物理的な黒球センサーが無いため）気温で近似しています。レポートされる`wbgt_risk_level`（注意／警戒／厳重警戒／危険）は環境省の公式な閾値（25/28/31°C）を使用しています。

`hal_env.cpp`も同様に10分ごとに計測を行うバックグラウンドタスクを持ち、WBGTが28℃以上（環境省の「厳重警戒」レベル）になった場合に画面上にアラートを表示し通知音を鳴らします。上記のCO2換気アラートと同様、閾値超過時に一度だけ発火して、下がるまで再アームされません（同様にクワイエットアワー中は抑制されます）。

### 待機時の画面減光／クワイエットアワー中の消灯

誰も操作していないときの省電力・光や気配の抑制のため、AIエージェントのアバター画面を自動的に減光する機能を追加：

- `main/hal/board/stackchan_display.cc`（`StackChanAvatarDisplay::UpdateStatusBar`/`SetStatus`）— 待機状態（聞き取り・発話のどちらでもない状態）が10秒続くと、バックライトが明るさ10%まで落ちます。クワイエットアワー中（23:00〜07:00、上記CO2/WBGTアラートと同じ`Hal::isQuietHours()`の時間帯）は、10%ではなく完全に消灯します。会話が再開（ウェイクワード、アバターへのタップなど）した瞬間に自動で元の明るさへ復帰します。
- `main/hal/board/stackchan.cc`（`CustomBacklight::SetBrightnessImpl`）— 明るさを変更するたびにPMICへのI2C書き込みが最大450ms程度にわたって何十回も無駄に発生していた既存のバグを修正しました（ベースとなる`Backlight`クラスのソフトウェアフェード処理は、内部の明るさ値を5msごとに1ずつ変化させますが、この機体の`SetBrightnessImpl`はその段階的な値を無視して、毎回最終的な目標値をそのまま書き込み直していました）。この内部I2Cバスはオーディオコーデックやタッチセンサー、IMUと共有されているため、会話中に明るさが変化すると、この無駄な書き込みによって音声再生やアバターのアニメーションが止まってしまうことがありました。現在は明るさを1回の書き込みで即座に適用し、フェード用タイマーも直後に止めるようにしています——そもそもこのPMICのハードウェア自体、滑らかなフェードには対応しておらず、粗い8段階のレジスタ値があるだけです。

### CO2/WBGTアラートの音声読み上げ（AquesTalk ESP32）

上記のCO2換気アラート・WBGT熱中症アラートに、実際の音声読み上げを追加しました（これまでは画面上の吹き出し表示＋通知音のみでした）：

- `main/hal/hal_tts.cpp` — AquesTalk ESP32の規則音声合成エンジンをラップした薄いレイヤー（`Hal::speakSymbols()`）。AquesTalkの音声記号列を`CAqTkPicoF_SetKoe`/`SyntheFrame`に渡し、8kHzの出力をこの機体のオーディオパイプライン（24kHz）に合わせて3倍アップサンプリング（`AqResample_Conv`）した上で、アバター自身のスピーカーへ直接書き込みます。
- `main/hal/board/hal_bridge.h`/`stackchan.cc`（`board_output_pcm`）— 生のPCMバッファを`Board::GetAudioCodec()`へ直接書き込み、`AudioService`のデコード・再生キュー（事前エンコード済みのOGGアセットしか再生できません）を経由しません。AIの音声出力とは同期していないため、呼び出し側は事前に`hal_bridge::is_xiaozhi_idle()`を確認する必要があります。`hal.cpp`内のアラートハンドラーでは、会話中はどちらのオーディオも壊さないよう読み上げ部分をスキップします（画面表示と通知音はそのまま維持されます）。
- `main/hal/hal.cpp`（`onCo2VentilationAlert`/`onWbgtAlert`ハンドラー）— 「二酸化炭素濃度は812ピーピーエムです。換気してください。」のように読み上げます。AquesTalkの`<NUMK VAL=... COUNTER=...>`という桁読みタグを使うことで、任意の数値を正しい日本語の音変化（連濁・促音化など）付きで、完全にオフラインかつAquesTalkの約2MBある漢字辞書を使わずに読み上げられます。各フレーズの定型部分は手書きの音声記号列で、数値部分だけを実行時に生成しています。

注記: AquesTalk ESP32はクローズドソースであり、評価版には有償のライセンスキーを設定するまで固定の制約があります（な行・ま行がすべて「ヌ」と発声されます）。これは上記のBSECの注記と同種のライセンス制約であるため、**このSDK自体はこのリポジトリにはコミットしていません**（`firmware/components/`は既にgitignore対象です）。入手・配置方法は、下記Buildセクションの「AquesTalk ESP32 SDK」を参照してください。

## Build

### AquesTalk ESP32 SDK (required for spoken alerts)

The CO2/WBGT spoken-alert feature above links against [AquesTalk ESP32](https://www.a-quest.com/products/aquestalk_esp32.html), a proprietary speech-synthesis SDK. It isn't part of this repo (see the license note above), so it has to be placed manually:

1. Download the evaluation SDK zip (or a licensed build, once purchased) from the AquesTalk ESP32 product page.
2. Copy these two files out of the zip into `firmware/components/aquestalk/`:
   - `src/aquestalk.h` → `firmware/components/aquestalk/include/aquestalk.h`
   - `src/esp32s3/libaquestalk_s.a` → `firmware/components/aquestalk/lib/esp32s3/libaquestalk_s.a`
3. `firmware/components/aquestalk/CMakeLists.txt` (checked into this repo) wraps the prebuilt library as an ESP-IDF component; nothing else to configure.

The kanji dictionary (`aq_dic/aqdic_m.bin`) is **not** used or needed — see the note above about the `<NUMK>` tag avoiding it. If it's ever wired in later, be aware it's ~2MB and this board's 16MB flash only has ~60KB of headroom left after the existing partitions (nvs/ota_0/ota_1/assets/coredump), so it wouldn't fit without shrinking something (`ota_1`, most likely, since automatic OTA is disabled — see above).

### Fetch Dependencies

```bash
python3 ./fetch_repos.py
```

### Tool Chains

[ESP-IDF v5.5.4](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/index.html)

### Build

```bash
idf.py build
```

### Host-side tests

The motion coordinate helpers can be tested without ESP-IDF hardware:

```bash
cmake -S tests -B build-host-tests
cmake --build build-host-tests
ctest --test-dir build-host-tests --output-on-failure
```

### Flash

```bash
idf.py flash
```
