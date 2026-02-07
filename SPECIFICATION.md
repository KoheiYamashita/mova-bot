# MOVA - 4D Omni-Wheel RC 制御システム仕様書

## 1. プロジェクト概要

4輪オムニホイールラジコンの制御基板を **M5Stack Core S3** + **TB6612FNG** に置き換える。
M5Stack Core S3 をサーバーとして動作させ、外部端末（スマートフォン・PC等）から WiFi 経由で操作する。

## 2. ハードウェア構成

### 2.1 メインコントローラー

| 項目 | 仕様 |
|------|------|
| デバイス | M5Stack Core S3 |
| SoC | ESP32-S3 (Dual-core Xtensa LX7, 240MHz) |
| Flash / PSRAM | 16MB / 8MB |
| ディスプレイ | 2.0インチ IPS タッチパネル (320x240) |
| カメラ | GC0308 (0.3MP, 640x480) |
| スピーカー | 1W (AW88298 I2S アンプ) |
| マイク | ES7210 デュアルマイク |
| 通信 | WiFi 802.11 b/g/n, BLE 5.0 |

### 2.2 モータードライバー

| 項目 | 仕様 |
|------|------|
| デバイス | TB6612FNG x2 |
| チャンネル数 | 2ch/個 (計4ch で4モーター対応) |
| 連続電流 | 1.2A/ch (ピーク 3.2A) |
| モーター電源電圧 | 2.5V〜13.5V DC |
| ロジック電源電圧 | 2.7V〜5.5V DC |
| PWM周波数 | 最大100kHz (IC単体性能。本システムでは PCA9685 経由のため最大1526Hz) |

### 2.3 PWM/GPIO エキスパンダー

M5Stack Core S3 の外部利用可能 GPIO は Port A: GPIO1/2 (I2C)、Port B: GPIO8/9、Port C: GPIO17/18 の計6本に限られる。
4モーター制御に必要な12本の信号線（PWM x4 + 方向制御 x8）を汎用 GPIO だけでは確保できないため、
Port A の I2C バスに **PCA9685**（16ch 12bit PWM ドライバー）を接続して信号線を拡張する。

| 項目 | 仕様 |
|------|------|
| デバイス | PCA9685 |
| チャンネル数 | 16ch (12bit PWM) |
| PWM周波数 | 24Hz〜1526Hz (内蔵 25MHz 発振器、8bit プリスケーラ) |
| 接続 | I2C (Port A: GPIO1/GPIO2) |
| I2Cアドレス | 0x40 (デフォルト) |

### 2.4 GPIO ピンアサイン

#### M5Stack Core S3 ピン使用状況

| GPIO | 用途 | 備考 |
|------|------|------|
| GPIO1 (Port A SCL) | PCA9685 I2C SCL | I2C バス |
| GPIO2 (Port A SDA) | PCA9685 I2C SDA | I2C バス |
| GPIO8 (Port B) | 予備 | 拡張用 |
| GPIO9 (Port B) | 予備 | 拡張用 |
| GPIO17 (Port C) | 予備 | 拡張用 |
| GPIO18 (Port C) | 予備 | 拡張用 |
| GPIO0/13/14/33/34 | I2S (スピーカー/マイク) | 内部使用 |

#### PCA9685 → TB6612FNG 配線

| PCA9685 Ch | 接続先 | 機能 |
|------------|--------|------|
| CH0 | TB6612FNG #1 PWMA | モーター1 速度 |
| CH1 | TB6612FNG #1 AIN1 | モーター1 方向1 |
| CH2 | TB6612FNG #1 AIN2 | モーター1 方向2 |
| CH3 | TB6612FNG #1 PWMB | モーター2 速度 |
| CH4 | TB6612FNG #1 BIN1 | モーター2 方向1 |
| CH5 | TB6612FNG #1 BIN2 | モーター2 方向2 |
| CH6 | TB6612FNG #2 PWMA | モーター3 速度 |
| CH7 | TB6612FNG #2 AIN1 | モーター3 方向1 |
| CH8 | TB6612FNG #2 AIN2 | モーター3 方向2 |
| CH9 | TB6612FNG #2 PWMB | モーター4 速度 |
| CH10 | TB6612FNG #2 BIN1 | モーター4 方向1 |
| CH11 | TB6612FNG #2 BIN2 | モーター4 方向2 |
| CH12 | TB6612FNG #1 STBY | スタンバイ制御 |
| CH13 | TB6612FNG #2 STBY | スタンバイ制御 |
| CH14-15 | 未使用 | 予備 |

> **注意**: 上記テーブルの列順（機能の並び）は概要説明用です。実際のチャンネル割り当ては `docs/wiring.md` を正とします。

#### TB6612FNG 制御ロジック

| AIN1/BIN1 | AIN2/BIN2 | モーター動作 |
|-----------|-----------|-------------|
| HIGH | LOW | 正転 (CW) |
| LOW | HIGH | 逆転 (CCW) |
| HIGH | HIGH | ショートブレーキ |
| LOW | LOW | 停止 |

## 3. 通信プロトコル

### 3.1 通信方式

WiFi を主要通信手段とする。ESP32-S3 は Classic Bluetooth (BR/EDR) 非対応のため BLE のみ利用可能だが、
カメラ映像ストリーミングには帯域が不足する。よって **WiFi ベースの HTTP サーバー方式** を採用する。

操作主体は AI エージェントを想定し、全機能を **HTTP REST API** で提供する。

| 機能 | プロトコル | 用途 |
|------|-----------|------|
| カメラ映像 | HTTP (MJPEG ストリーム, **port 81**) | リアルタイム映像配信 |
| 制御コマンド | HTTP REST API (`POST /command`, port 80) | モーター・絵文字・音声を単一APIで制御 |
| 状態取得 | HTTP REST API (`GET /status`, port 80) | 接続状態・バッテリー等 |
| 緊急停止 | HTTP REST API (`POST /emergency_stop`, port 80) | 全モーター即時停止 |

> **注意**: MJPEG ストリーミングは ESPAsyncWebServer の multipart 制約のため、ESP-IDF httpd を用いて **port 81** で配信する。REST API およびその他の機能は ESPAsyncWebServer により **port 80** で提供する。

### 3.2 WiFi 動作モード

**STA モード（既存ネットワーク接続）** のみとする。WiFi 接続情報は microSD カード上の設定ファイルから読み込む。

#### 設定ファイル

パス: `/wifi.txt` (microSD カードルート)

```
SSID=MyNetwork
PASS=mypassword
```

- UTF-8 テキスト、改行区切りの `KEY=VALUE` 形式
- 起動時に読み込み、記載された SSID に接続を試みる
- 接続成功時: ディスプレイに取得した IP アドレスを表示
- 接続失敗時 (ファイル未挿入・SSID 不一致等): ディスプレイにエラーを表示し、リトライを継続

## 4. API 仕様

### 4.1 カメラ

#### 映像ストリーム

```
GET /stream   (port 81)
```

- **ポート 81** で配信（REST API の port 80 とは異なる）
- Content-Type: `multipart/x-mixed-replace; boundary=frame`
- MJPEG 形式で連続配信
- クエリパラメータ:
  - `quality` (int, 10-63, デフォルト: 12) - JPEG品質 (低い方が高画質)
  - `resolution` (string, デフォルト: "QVGA") - 解像度 (`QQVGA`, `QVGA`, `VGA`)

#### 静止画キャプチャ

```
GET /capture
```

- Content-Type: `image/jpeg`
- 単一フレーム取得

### 4.2 制御コマンド

```
POST /command
Content-Type: application/json
```

モーター制御・絵文字表示・音声再生を **単一のエンドポイント** で行う。
各フィールドはすべてオプションで、含まれたものだけが実行される。

- Request:
```json
{
  "motors": [
    {"id": 0, "speed": 4095, "direction": "cw"},
    {"id": 1, "speed": 2048, "direction": "ccw"},
    {"id": 2, "speed": 0, "direction": "stop"},
    {"id": 3, "speed": 3200, "direction": "brake"}
  ],
  "emoji": "😎",
  "audio": {
    "sample_rate": 16000,
    "bits": 16,
    "channels": 1,
    "data": "<Base64 エンコード PCM データ>"
  }
}
```

#### motors フィールド (オプション)

| フィールド | 型 | 値 | 説明 |
|-----------|------|------|------|
| `id` | int | 0-3 | モーター番号 |
| `speed` | int | 0-4095 | PWM値 (12bit, PCA9685準拠) |
| `direction` | string | `"cw"`, `"ccw"`, `"brake"`, `"stop"` | 回転方向 |

- 各モーターは独立して制御する（上位の運動学的変換はクライアント側の責務）
- `motors` 配列には変更したいモーターのみ含めればよい（差分更新）

#### emoji フィールド (オプション)

| 型 | 値 | 説明 |
|------|------|------|
| string | 絵文字文字列 or `""` | ディスプレイ全画面に表示。空文字列で無表情に戻す |

- フィールドが省略された場合、現在の表示を維持する（更新なし）
- 起動時の初期表示は **無表情 (😐)**
- 対応する絵文字画像はファームウェアにプリセットとして格納する

#### audio フィールド (オプション)

| フィールド | 型 | 値 | 説明 |
|-----------|------|------|------|
| `sample_rate` | int | 8000/16000/44100 | サンプルレート (Hz) |
| `bits` | int | 8/16 | ビット深度 |
| `channels` | int | 1 | チャンネル数 (モノラル) |
| `data` | string | Base64 | PCM 音声データ (リトルエンディアン) を Base64 エンコードした文字列 |

- M5Stack 側で Base64 デコード → PSRAM に PCM データ全体を確保 → I2S 経由でスピーカーに出力する
- 全データ受信後に一括再生する（ストリーミング再生は非対応）

#### 使用例

モーターのみ:
```json
{"motors": [{"id": 0, "speed": 4095, "direction": "cw"}]}
```

絵文字のみ:
```json
{"emoji": "🔥"}
```

全部同時:
```json
{
  "motors": [{"id": 0, "speed": 4095, "direction": "cw"}],
  "emoji": "🔥",
  "audio": {"sample_rate": 16000, "bits": 16, "channels": 1, "data": "AQAC..."}
}
```

#### レスポンス

- 200 OK:
```json
{
  "status": "ok"
}
```

- 400 Bad Request:
```json
{
  "status": "error",
  "message": "Invalid motor id: 5"
}
```

### 4.3 緊急停止

```
POST /emergency_stop
```

- 全モーターを即座に停止する（リクエストボディ不要）

- Response (200 OK):
```json
{
  "status": "ok"
}
```

### 4.4 システム状態

```
GET /status
```

- Response (200 OK):
```json
{
  "battery_level": 85,
  "wifi_rssi": -45,
  "motor_enabled": true,
  "uptime_sec": 1234,
  "current_emoji": "😀",
  "motor_states": [
    {"id": 0, "speed": 4095, "direction": "cw"},
    {"id": 1, "speed": 2048, "direction": "ccw"},
    {"id": 2, "speed": 0, "direction": "stop"},
    {"id": 3, "speed": 3200, "direction": "cw"}
  ]
}
```

## 5. ファームウェア設計

### 5.1 開発環境

| 項目 | 選定 |
|------|------|
| フレームワーク | Arduino (ESP32) |
| ライブラリ | M5Unified, ESP32 Camera, ESPAsyncWebServer, AsyncTCP, Adafruit PWMServoDriver (PCA9685), SD |
| IDE | PlatformIO (推奨) or Arduino IDE |

### 5.2 タスク構成 (FreeRTOS)

ESP32-S3 のデュアルコアを活用し、FreeRTOS タスクで並行処理する。

| タスク名 | Core | 優先度 | 役割 |
|----------|------|--------|------|
| `TaskMotorControl` | Core 0 | 高 | モーター制御 (PCA9685 I2C 通信) |
| `TaskAudioPlayback` | Core 0 | 中 | 音声バッファ再生 (I2S 出力) |
| `TaskWebServer` | Core 1 | 高 | HTTP サーバー (REST API) |
| `TaskCameraStream` | Core 1 | 中 | カメラキャプチャ・MJPEG配信 |
| `TaskDisplay` | Core 1 | 低 | 絵文字描画・UI更新 |

### 5.3 ソースコード構成

```
MOVA/
├── SPECIFICATION.md          # 本仕様書
├── platformio.ini            # PlatformIO 設定
├── src/
│   ├── main.cpp              # エントリーポイント・タスク起動
│   ├── config.h              # 定数・ピン定義・設定
│   ├── wifi_config.h         # SD カード WiFi 設定読み込み
│   ├── wifi_config.cpp
│   ├── motor_controller.h    # モーター制御クラス
│   ├── motor_controller.cpp
│   ├── web_server.h          # HTTP サーバー / REST API ハンドラー
│   ├── web_server.cpp
│   ├── camera.h              # カメラ初期化・キャプチャ
│   ├── camera.cpp
│   ├── audio_player.h        # 音声再生 (I2S)
│   ├── audio_player.cpp
│   ├── display.h             # ディスプレイ制御・絵文字表示
│   ├── display.cpp
│   └── emoji_data.h          # 絵文字ビットマップデータ
├── data/                     # SPIFFS ファイル (Web UI 等)
│   └── index.html            # 操作用 Web UI
└── docs/
    └── wiring.md             # 配線図
```

### 5.4 制御コマンド処理フロー

```
クライアント (AI エージェント等)
    │
    │ POST /command  {"motors":[...], "emoji":"🔥", "audio":{...}}
    ▼
HTTP ハンドラー (TaskWebServer)
    │
    ├─ motors あり → xQueueSend(motorQueue) → TaskMotorControl
    │                                              │
    │                                              │ PCA9685 I2C 書き込み
    │                                              ▼
    │                                           PCA9685 → TB6612FNG → モーター M0〜M3
    │
    ├─ emoji あり  → xQueueSend(displayQueue) → TaskDisplay
    │                                              │
    │                                              ▼
    │                                           ディスプレイ全画面描画
    │
    └─ audio あり  → Base64デコード → xStreamBufferSend(audioBuffer) → TaskAudioPlayback
                                                                          │
                                                                          │ I2S 出力 (AW88298)
                                                                          ▼
                                                                       スピーカー
```

### 5.5 絵文字表示

- 絵文字はプリセット画像としてファームウェアに組み込む
- 320x240 ディスプレイ全画面にスプライトとして描画
- 起動時は **無表情 (😐)** を表示
- `POST /command` の `emoji` フィールドで更新。省略時は現在の表示を維持

## 6. 電源設計

| 項目 | 仕様 |
|------|------|
| モーター電源 | 外部バッテリー 7.4V〜12V (2S〜3S LiPo) |
| ロジック電源 | DIN Base 内蔵バッテリー (3.7V 500mAh) または USB-C 給電 ※CoreS3 標準キットに DIN Base 同梱 |
| TB6612FNG VM | モーター電源から直結 |
| TB6612FNG VCC | M5Stack 5V 出力から供給 |
| PCA9685 VCC | M5Stack 3.3V (CoreS3 の I2C は 3.3V ロジックのため、レベルシフタなしで直結可能) |

**注意**: モーター電源とロジック電源のGNDは必ず共通接続すること。

## 7. 安全機構

| 機能 | 説明 |
|------|------|
| ウォッチドッグ | モーター制御コマンドが 3 秒以上途絶えた場合に全モーター停止 |
| 電流保護 | TB6612FNG 内蔵の過電流保護 |
| バッテリー監視 | AXP2101 PMU によるバッテリー電圧監視・低電圧警告 |
| 緊急停止 | `POST /emergency_stop` で即座に全モーター停止 |

## 8. クライアント要件

操作主体は **AI エージェント** を想定する。標準的な HTTP クライアントから REST API を呼び出して制御する。

### 8.1 想定クライアント

- AI エージェント（HTTP リクエストでモーター制御・音声再生・絵文字表示を実行）
- curl / スクリプト等によるデバッグ・手動操作

### 8.2 簡易 Web UI（デバッグ用）

SPIFFS に簡易 Web UI を格納し、ブラウザからもアクセス可能とする。

- **カメラビュー**: MJPEG ストリームのリアルタイム表示
- **モーター制御パネル**: 4モーターそれぞれの速度スライダー (0-4095) と方向ボタン (CW/CCW/Stop/Brake)  → `POST /command`
- **絵文字セレクター**: 絵文字一覧から選択して送信 → `POST /command`
- **緊急停止ボタン**: `POST /emergency_stop`
- **ステータス表示**: バッテリー残量、WiFi 強度、接続状態

### 8.3 技術スタック

- HTML5 / CSS / Vanilla JavaScript (フレームワーク不使用、軽量化)
- Fetch API (REST API 呼び出し)

## 9. 制約・前提条件

- ESP32-S3 は Classic Bluetooth (BR/EDR) 非対応。BLE 5.0 のみ利用可能
- カメラ (GC0308) は 0.3MP (640x480) が上限。高解像度は不可
- WiFi と BLE の同時使用は可能だが、パフォーマンスに影響あり
- MJPEG ストリーミングと REST API 制御の同時処理はデュアルコアで分散
- PCA9685 の I2C 通信速度がモーター制御のレイテンシに影響する (400kHz Fast Mode で約 1ms/コマンド)
- 音声はストリーミング再生ではなく、全データ受信後に一括再生する方式とする（リアルタイム音声通話は非対応）
