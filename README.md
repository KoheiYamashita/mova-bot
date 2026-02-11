# MOVA

M5Stack Core S3 ベースの4輪オムニホイールロボット。WiFi 経由の REST API で AI エージェントが自律制御する。

## 主な機能

- **4方向オムニホイール移動** (前後左右 + 回転)
- **カメラ映像** (GC0308, MJPEG ストリーミング対応)
- **絵文字ディスプレイ** (320x240, 8種プリセット)
- **スピーカー音声再生** + **マイク録音**
- **4方向 ToF 障害物検知** + 自動緊急停止

## ハードウェア

### 電子部品

| 品名 | 数量 | 用途 |
|------|------|------|
| M5Stack Core S3 | 1 | メインコントローラー |
| 4ch Encoder Motor Module (M138) | 1 | モータードライバー (M5-Bus スタック) |
| PaHUB2 (TCA9548A) | 1 | I2C マルチプレクサ |
| VL53L0X ToF センサー (Grove) | 4 | 障害物検知 (前後左右) |
| PD対応モバイルバッテリー (12V出力) | 1 | 電源 |
| PD トリガーケーブル (USB-C → DC5521 12V) | 1 | バッテリー → M138 接続 |
| Grove ケーブル (4pin) | 5 | PaHUB2 + VL53L0X 接続 |
| microSD カード | 1 | WiFi 設定ファイル格納 |

### 車体

[ラジコンカー (Amazon: B08K2RQSYV)](https://www.amazon.co.jp/dp/B08K2RQSYV) をベースに使用。上蓋と制御基板を取り外し、シャーシ・モーター・ホイールを流用する。上蓋の代わりに 3D プリント製カバーを被せる。

### 接続構成

```
PD バッテリー ──[12V]──→ M138 DC5521
                           │
                      ┌────┴────┐
                      │  M138   │→ モーター x4
                      ├─────────┤
                      │ Core S3 │
                      └────┬────┘
                        Port A
                           │
                      ┌────┴────┐
                      │ PaHUB2  │
                      └─┬─┬─┬─┬┘
                       前 右 後 左
                      (VL53L0X x4)
```

配線の詳細は [docs/wiring.md](docs/wiring.md) を参照。

## ビルド & デプロイ

### 前提条件

- [PlatformIO](https://platformio.org/) (CLI or VS Code 拡張)
- USB-C ケーブル

### WiFi 設定

microSD カードのルートに `wifi.txt` を作成:

```
SSID=YourNetwork
PASS=YourPassword
```

### ファームウェア書き込み

```bash
# ビルド
pio run

# 書き込み (Core S3 を USB-C で接続)
pio run --target upload

# シリアルモニタ
pio device monitor
```

## API

ベース URL: `http://<デバイスIP>:80`

| エンドポイント | メソッド | 説明 |
|---------------|---------|------|
| `/status` | GET | デバイス状態 (バッテリー, センサー, モーター) |
| `/capture` | GET | カメラ JPEG 1枚取得 |
| `/stream` (port 81) | GET | MJPEG ストリーミング |
| `/command` | POST | 統合制御 (モーター + 絵文字 + 音声) |
| `/mic/record` | POST | マイク録音 |
| `/emergency_stop` | POST | 全モーター緊急停止 |

### /command リクエスト例

```json
{
  "motors": [
    {"id": 0, "speed": 2048, "direction": "cw"},
    {"id": 1, "speed": 2048, "direction": "ccw"}
  ],
  "emoji": "😎",
  "audio": {
    "sample_rate": 16000,
    "bits": 16,
    "channels": 1,
    "data": "<Base64 PCM>"
  }
}
```

全フィールドはオプション。必要なものだけ含める。

API の詳細は [docs/SPECIFICATION.md](docs/SPECIFICATION.md) を参照。

## プロジェクト構成

```
MOVA/
├── platformio.ini           # ビルド設定
├── src/                     # ファームウェアソース
│   ├── main.cpp             # エントリーポイント・タスク起動
│   ├── config.h             # 定数・ピン定義
│   ├── motor_controller.*   # M138 モーター制御
│   ├── obstacle_sensor.*    # PaHUB2 + VL53L0X 障害物検知
│   ├── camera.*             # GC0308 カメラ
│   ├── web_server.*         # REST API
│   ├── display.*            # 絵文字ディスプレイ
│   ├── audio_player.*       # I2S 音声再生
│   ├── mic_recorder.*       # ES7210 マイク録音
│   └── wifi_config.*        # SD カード WiFi 設定
├── data/                    # Web UI (LittleFS)
├── docs/
│   ├── SPECIFICATION.md     # 詳細仕様書
│   └── wiring.md            # 配線ガイド
├── tools/
│   └── generate_emoji.py    # 絵文字ビットマップ生成
└── .claude/skills/          # Claude Code スキル
    ├── mova/                # デバイス制御スキル
    └── setup/               # セットアップウィザード
```

## 安全機構

| 機能 | 説明 |
|------|------|
| 障害物検知 | 4方向 ToF センサー、150mm 以下で自動緊急停止 |
| 方向ブロック | 障害物方向へのモーターコマンドを自動拒否 |
| Fail-Closed | センサー異常時は全モーター停止 |
| ウォッチドッグ | 3秒間コマンドなしで全モーター停止 |
| 緊急停止 API | `POST /emergency_stop` で即時停止 |

## Claude Code 連携

`/mova` スキルで AI エージェントがロボットを自律制御できる:

```
/mova                      # 自律行動モード (探索・感情表現)
/mova ステータス確認        # 状態取得
/mova 前に進んで           # 移動指示
/mova 「こんにちは」と喋って  # TTS 音声再生
```

`/setup` スキルで対話的にゼロからセットアップ:

```
/setup                     # 続きから再開
/setup 3_wiring            # 配線ステップへジャンプ
```

## ライブラリ

| ライブラリ | 用途 |
|-----------|------|
| [M5Unified](https://github.com/m5stack/M5Unified) | M5Stack ハードウェア抽象化 |
| [M5Module-4EncoderMotor](https://github.com/m5stack/M5Module-4EncoderMotor) | M138 モータードライバー |
| [VL53L0X](https://github.com/pololu/vl53l0x-arduino) | ToF センサー |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | JSON パース |
| [ESPAsyncWebServer](https://github.com/mathieucarbou/ESPAsyncWebServer) | HTTP サーバー |
