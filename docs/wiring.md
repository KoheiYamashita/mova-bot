# MOVA 配線メモ

## M5Stack Core S3 → PCA9685 (I2C)

| Core S3 Port A | PCA9685 |
|----------------|---------|
| GPIO2 (SDA)    | SDA     |
| GPIO1 (SCL)    | SCL     |
| GND            | GND     |
| 5V             | VCC     |

> **注意:** I2C ピン (GPIO1/2) は要実機検証。Core S3 Port A のピン配置を確認すること。

## PCA9685 → TB6612FNG チャンネルマッピング

### TB6612FNG #1 (Motor 0, Motor 1)

| PCA9685 CH | TB6612FNG #1 | 機能       |
|------------|-------------|-----------|
| CH0        | AIN1        | Motor 0 方向 |
| CH1        | AIN2        | Motor 0 方向 |
| CH2        | PWMA        | Motor 0 速度 |
| CH3        | BIN1        | Motor 1 方向 |
| CH4        | BIN2        | Motor 1 方向 |
| CH5        | PWMB        | Motor 1 速度 |
| CH12       | STBY        | スタンバイ    |

### TB6612FNG #2 (Motor 2, Motor 3)

| PCA9685 CH | TB6612FNG #2 | 機能       |
|------------|-------------|-----------|
| CH6        | AIN1        | Motor 2 方向 |
| CH7        | AIN2        | Motor 2 方向 |
| CH8        | PWMA        | Motor 2 速度 |
| CH9        | BIN1        | Motor 3 方向 |
| CH10       | BIN2        | Motor 3 方向 |
| CH11       | PWMB        | Motor 3 速度 |
| CH13       | STBY        | スタンバイ    |

## STBY ピン初期状態の注意事項

- PCA9685 の出力は起動直後レベルが不定
- モーターが意図せず動作するのを防ぐため、以下の対策を検討:
  1. STBY ピンに外付けプルダウン抵抗 (10kΩ) を追加
  2. ファームウェア初期化時に STBY を LOW に設定してからモーター制御を開始
- Phase 2 で実装・検証予定

## 電源

- PCA9685 ロジック: Core S3 Port A の 5V から供給
- PCA9685 V+: モーター用外部電源 (バッテリー) から供給
- TB6612FNG VM: PCA9685 V+ と共有
