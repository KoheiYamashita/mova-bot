# MOVA 実装計画 — 4D オムニホイール RC 制御システム

## Context

M5Stack Core S3 + PCA9685 + TB6612FNG x2 による4輪オムニホイール RC カーの制御ファームウェアを新規開発する。WiFi HTTP REST API でモーター制御・カメラ映像・絵文字表示・音声再生を提供し、AI エージェントからの操作を主な利用形態とする。プロジェクトディレクトリには現在 `SPECIFICATION.md` のみが存在し、全コードをゼロから実装する。

---

## アーキテクチャ上の重要な設計判断

### 1. MJPEG ストリーミング: ハイブリッドサーバー方式
- **ESPAsyncWebServer (port 80)**: REST API (`/command`, `/status`, `/emergency_stop`, `/capture`) + 静的ファイル配信
- **ESP-IDF httpd (port 81)**: MJPEG `/stream` エンドポイント
- 理由: ESPAsyncWebServer は `multipart/x-mixed-replace` の連続フレーム送信に制約がある。ESP-IDF httpd は Espressif 公式の実績ある方式

### 2. GC0308 カメラ: ソフトウェア JPEG 変換
- GC0308 はハードウェア JPEG エンコード非対応 → RGB565 でキャプチャし `frame2jpg()` で変換
- デフォルト QVGA (320x240) でストリーミング、VGA は `/capture` 単発用

### 3. 絵文字: JPEG 圧縮画像を PROGMEM に格納
- 320x240 RGB565 生データは 150KB/枚 → JPEG 圧縮で 8-15KB/枚に削減
- `M5.Display.drawJpg()` でデコード描画
- 初期セット 8 種 (😐😀😎🔥😢😡❤️⚡) ≒ 96KB

### 4. 音声: 全バッファ受信後再生方式 (SPEC 準拠)
- Web サーバーが Base64 デコード → PSRAM に PCM データ全体を格納 → Audio タスクに再生指示 → `M5.Speaker.playRaw()` で一括再生
- ストリーミング逐次供給は行わない（仕様書の「バッファリング後に再生」に準拠）

### 5. I2C バス共有
- M5Unified 内部 I2C と PCA9685 が Port A を共有 → FreeRTOS Mutex で排他制御

---

## 実装フェーズ

### Phase 0: プロジェクト基盤構築
**ファイル**: `platformio.ini`, 全ソースファイルの空スケルトン, `src/main.cpp`（最小起動確認）

- PlatformIO 設定: board=`m5stack-cores3`, framework=`arduino`
- ライブラリ: M5Unified, esp32-camera, ESPAsyncWebServer, AsyncTCP, Adafruit PWM Servo Driver, ArduinoJson
- ビルドフラグ: PSRAM有効, USB CDC, SPIFFS
- **検証**: `pio run` でビルド成功、デバイスにフラッシュして "MOVA Booting..." 表示

### Phase 1: コア基盤 — 設定 + WiFi
**ファイル**: `src/config.h`, `src/wifi_config.h`, `src/wifi_config.cpp`

- `config.h`: 全ハードウェア定数 (ピン定義, PCA9685チャンネルマッピング, タスク設定, キューサイズ等)
  - `MotorPinMap` 構造体 + `MOTOR_PINS[4]` constexpr 配列
  - `MotorDirection` enum class (STOP/CW/CCW/BRAKE)
  - `MotorState` 構造体
- `wifi_config`: SD カードから `/wifi.txt` を読み込み (`KEY=VALUE` パース)、WiFi STA 接続 (リトライ付き)
- **検証**: SD カード有無でのブート動作確認、WiFi 接続・IP 表示

### Phase 2: モーター制御
**ファイル**: `src/motor_controller.h`, `src/motor_controller.cpp`

- `MotorController` クラス:
  - `begin()`: PCA9685 初期化 (I2C 400kHz, PWM 1000Hz), STBY ピン HIGH
  - `setMotor(id, speed, direction)`: I2C Mutex 保護下で PWM + 方向ピン設定
  - `emergencyStop()`: 全モーター即時停止
  - `feedWatchdog()` / `checkWatchdog()`: 3秒タイムアウト
- `MotorCommand` 構造体 (キューメッセージ)
- `taskMotorControl()`: FreeRTOS タスク (Core 0, 高優先度)
  - キュー受信 → コマンド実行、タイムアウト時 → ウォッチドッグチェック
- TB6612FNG 制御ロジック: CW=HIGH/LOW, CCW=LOW/HIGH, BRAKE=HIGH/HIGH, STOP=LOW/LOW
- PCA9685 デジタル出力: `setPWM(ch, 4096, 0)` = HIGH, `setPWM(ch, 0, 4096)` = LOW
- **検証**: I2C スキャンで PCA9685 検出、シリアル経由で各モーター各方向テスト、ウォッチドッグ動作確認

### Phase 3: Web サーバー / REST API
**ファイル**: `src/web_server.h`, `src/web_server.cpp`

- `MOVAWebServer` クラス (AsyncWebServer ラッパー):
  - `POST /command`: ArduinoJson でパース → motors/emoji/audio を各キューへ振り分け
  - `POST /emergency_stop`: `motorQueue` にフロント挿入 (最優先)
  - `GET /status`: バッテリー, WiFi RSSI, モーター状態, uptime, 現在の絵文字
  - `GET /capture`: `esp_camera_fb_get()` → JPEG 変換 → レスポンス
  - SPIFFS 静的ファイル配信 (`/` → `index.html`)
  - CORS ヘッダー (`Access-Control-Allow-Origin: *`)
- audio の Base64 デコード: `mbedtls_base64_decode()` → PSRAM に `ps_malloc()` で確保 → `audioQueue` に `AudioCommand` を送信
- **検証**: curl で全エンドポイントテスト (正常系 + 異常系)

### Phase 4: カメラ / MJPEG ストリーミング
**ファイル**: `src/camera.h`, `src/camera.cpp`

- `cameraInit()`: GC0308 ピン設定, RGB565, PSRAM フレームバッファ, ダブルバッファ
- `cameraCaptureJpeg()`: フレーム取得 → `frame2jpg()` → JPEG バッファ返却
- `cameraStreamServerStart()`: ESP-IDF httpd (port 81) で MJPEG ストリーム配信
  - `multipart/x-mixed-replace` boundary フレーミング
  - ~30fps キャップ (`vTaskDelay(33ms)`)
- **検証**: ブラウザで `http://<ip>:81/stream` 表示、`/capture` で JPEG ダウンロード

### Phase 5: ディスプレイ / 絵文字
**ファイル**: `src/display.h`, `src/display.cpp`, `src/emoji_data.h`

- `emoji_data.h`: 絵文字 JPEG データの PROGMEM バイト配列 + ルックアップテーブル (`findEmoji()`)
- `DisplayCommand` 構造体 (キューメッセージ)
- `displayEmoji()`: `M5.Display.drawJpg()` で全画面描画
- `displayStatus()`: ブートシーケンス用テキスト表示
- `taskDisplay()`: FreeRTOS タスク (Core 1, 低優先度) — キュー受信で絵文字更新
- 起動時デフォルト: 😐 (無表情)
- **検証**: curl で絵文字コマンド送信 → ディスプレイ更新確認

### Phase 6: 音声再生
**ファイル**: `src/audio_player.h`, `src/audio_player.cpp`

- `AudioCommand` 構造体 (sample_rate, bits, channels, pcmData ポインタ, pcmLength)
- `audioInit()`: M5.Speaker 設定
- `taskAudioPlayback()`: FreeRTOS タスク (Core 0, 中優先度)
  - `audioQueue` から `AudioCommand` を受信 → `M5.Speaker.playRaw()` で全データ一括再生 → 再生完了後に PSRAM バッファを解放
- Web サーバー側で Base64 デコード → PSRAM に `ps_malloc()` で PCM 全体を確保 → `AudioCommand` をキューに送信
- **検証**: Python で 440Hz サイン波 PCM 生成 → Base64 → curl 送信 → スピーカーから音確認

### Phase 7: Web UI (デバッグ用)
**ファイル**: `data/index.html`

- HTML5 / CSS / Vanilla JS のシングルページ (30KB 以下)
- セクション: カメラビュー (MJPEG), モーター制御パネル (速度スライダー+方向ボタン x4), 絵文字セレクター, 緊急停止ボタン (赤大), ステータスパネル (2秒ポーリング)
- Fetch API で REST API 呼び出し
- **検証**: ブラウザで全機能操作テスト

### Phase 8: 安全機構・統合・仕上げ
- ウォッチドッグ強制テスト (3秒無通信 → 全モーター停止)
- バッテリー低下警告
- 30分以上の連続動作テスト (メモリリーク監視)
- 同時負荷テスト (映像ストリーム + モーター制御 + 絵文字 + 音声)
- エラーケース (SD 無し, WiFi 切断, 不正 JSON, 未知の絵文字)

---

## フェーズ依存関係

Phase 4/5/6 は Phase 1 完了後に並列開発可能:

```
Phase 0 → Phase 1 → Phase 2 → Phase 3 ──→ Phase 7 → Phase 8
                  ↘ Phase 4 (カメラ)     ↗
                  ↘ Phase 5 (ディスプレイ) ↗
                  ↘ Phase 6 (音声)       ↗
```

---

## タスク間通信設計

### グローバルキュー/バッファ (main.cpp で生成)

| キュー/バッファ | メッセージ型 | サイズ | 方向 |
|---|---|---|---|
| `motorQueue` | `MotorCommand` | 8 個 | WebServer → MotorTask |
| `displayQueue` | `DisplayCommand` | 4 個 | WebServer → DisplayTask |
| `audioQueue` | `AudioCommand` | 2 個 | WebServer → AudioTask (PCM データは PSRAM ポインタ渡し) |

### main.cpp 起動シーケンス

1. M5Unified 初期化
2. ブート画面表示
3. SD カードから WiFi 設定読み込み
4. WiFi STA 接続 (リトライループ)
5. カメラ初期化
6. FreeRTOS キュー/バッファ生成
7. FreeRTOS タスク生成 (Motor@Core0, Audio@Core0, Display@Core1)
8. Web サーバー起動 (AsyncWebServer@80, httpd@81)
9. IP アドレスをシリアル + ディスプレイに表示

---

## 検証方法

### フェーズごとのテスト
- **WiFi**: SD カード有無, 接続成功/失敗
- **モーター**: シリアル制御 → curl 制御、全方向・ウォッチドッグ
- **API**: curl で全エンドポイント (正常系 + 異常入力)
- **カメラ**: ブラウザでストリーム確認
- **絵文字**: curl → ディスプレイ目視確認
- **音声**: Python 生成サイン波で聴覚確認

### 統合テスト
```bash
# ウォッチドッグテスト
curl -X POST http://<ip>/command -H "Content-Type: application/json" \
  -d '{"motors":[{"id":0,"speed":4095,"direction":"cw"}]}'
sleep 4
curl http://<ip>/status | jq '.motor_states[0]'  # speed=0 を確認

# メモリ監視 (main loop にログ追加)
# "Free heap: XXXX, PSRAM: XXXX" をシリアルモニタで30分観察
```
