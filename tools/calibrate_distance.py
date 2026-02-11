#!/usr/bin/env python3
"""MOVA ロボット距離キャリブレーションツール

壁に向かって配置したロボットを後退させ、ToFセンサー距離とカメラ画像を
ステップごとに記録する。モーター指令と実移動距離の関係を把握するための
キャリブレーション用スクリプト。

使い方:
    1. ロボットを壁に正面を向けて配置する
    2. 本スクリプトを実行:
       python3 tools/calibrate_distance.py --host 192.168.x.x
    3. 各ステップで後退→計測を繰り返し、結果をログ出力

出力:
    calibration_out/ ディレクトリに画像とCSVログを保存
"""

import argparse
import csv
import json
import os
import sys
import time
from datetime import datetime
from pathlib import Path

try:
    import requests
except ImportError:
    print("ERROR: requests ライブラリが必要です: pip install requests", file=sys.stderr)
    sys.exit(1)


# ── モーター設定 ──────────────────────────────────────────────────
# X配置 4輪オムニ: 後退は Motor 0,2=CCW / Motor 1,3=CW
BACKWARD_MOTORS = [
    {"id": 0, "direction": "ccw"},
    {"id": 1, "direction": "cw"},
    {"id": 2, "direction": "ccw"},
    {"id": 3, "direction": "cw"},
]

STOP_MOTORS = [
    {"id": 0, "direction": "stop", "speed": 0},
    {"id": 1, "direction": "stop", "speed": 0},
    {"id": 2, "direction": "stop", "speed": 0},
    {"id": 3, "direction": "stop", "speed": 0},
]


def get_status(base_url: str, timeout: float = 5.0) -> dict:
    """GET /status でロボットの現在状態を取得"""
    resp = requests.get(f"{base_url}/status", timeout=timeout)
    resp.raise_for_status()
    return resp.json()


def capture_image(base_url: str, timeout: float = 5.0) -> bytes:
    """GET /capture でカメラ画像 (JPEG) を取得"""
    resp = requests.get(f"{base_url}/capture", timeout=timeout)
    resp.raise_for_status()
    return resp.content


def send_command(base_url: str, payload: dict, timeout: float = 5.0) -> dict:
    """POST /command でモーター/絵文字/音声コマンドを送信"""
    resp = requests.post(
        f"{base_url}/command",
        json=payload,
        timeout=timeout,
    )
    resp.raise_for_status()
    return resp.json()


def emergency_stop(base_url: str, timeout: float = 5.0):
    """POST /emergency_stop で緊急停止"""
    resp = requests.post(f"{base_url}/emergency_stop", timeout=timeout)
    resp.raise_for_status()


def move_backward(base_url: str, speed: int, duration_ms: int):
    """後退モーター指令を送り、指定時間後に停止する

    Args:
        base_url: ロボットのベースURL
        speed: モーター速度 (0-4095)
        duration_ms: 移動時間 (ミリ秒)
    """
    motors = [
        {**m, "speed": speed} for m in BACKWARD_MOTORS
    ]
    send_command(base_url, {"motors": motors})
    time.sleep(duration_ms / 1000.0)
    send_command(base_url, {"motors": STOP_MOTORS})


def measure(base_url: str, settle_ms: int = 300) -> tuple[dict, bytes]:
    """静止後にToF距離とカメラ画像を同時取得

    Args:
        base_url: ロボットのベースURL
        settle_ms: 計測前の静定待ち時間(ms)

    Returns:
        (sensors_dict, jpeg_bytes)
    """
    time.sleep(settle_ms / 1000.0)
    status = get_status(base_url)
    image = capture_image(base_url)
    return status["sensors"], image


def print_sensors(sensors: dict, label: str = ""):
    """センサー値を見やすく表示"""
    prefix = f"[{label}] " if label else ""
    print(
        f"  {prefix}"
        f"Front={sensors['front_mm']}mm  "
        f"Right={sensors['right_mm']}mm  "
        f"Rear={sensors['rear_mm']}mm  "
        f"Left={sensors['left_mm']}mm  "
        f"(healthy={sensors['healthy']})"
    )


def run_calibration(
    base_url: str,
    output_dir: Path,
    speed: int,
    duration_ms: int,
    steps: int,
    settle_ms: int,
):
    """キャリブレーション実行のメインループ

    Args:
        base_url: ロボットのベースURL (例: http://192.168.1.100)
        output_dir: 画像とCSVの出力先ディレクトリ
        speed: 後退モーター速度 (0-4095)
        duration_ms: 1ステップの移動時間 (ms)
        steps: 繰り返し回数
        settle_ms: 計測前の静定待ち (ms)
    """
    output_dir.mkdir(parents=True, exist_ok=True)
    csv_path = output_dir / "calibration_log.csv"

    # CSVヘッダー
    fieldnames = [
        "step", "timestamp",
        "front_mm", "right_mm", "rear_mm", "left_mm",
        "delta_front_mm", "cumulative_front_mm",
        "speed", "duration_ms",
        "image_file",
    ]

    print("=" * 70)
    print("MOVA 距離キャリブレーション")
    print(f"  ロボット: {base_url}")
    print(f"  速度: {speed}/4095  移動時間: {duration_ms}ms  ステップ数: {steps}")
    print(f"  出力先: {output_dir}")
    print("=" * 70)
    print()

    # 初期状態の確認
    print("接続テスト中...")
    try:
        status = get_status(base_url)
    except Exception as e:
        print(f"ERROR: ロボットに接続できません: {e}", file=sys.stderr)
        sys.exit(1)

    print(f"  バッテリー: {status['battery_level']}%  WiFi RSSI: {status['wifi_rssi']}dBm")
    if not status.get("motor_enabled", False):
        print("WARNING: モーターが無効です。動作しない可能性があります。")
    print()

    # ── Step 0: 初期計測 ──
    print("Step 0: 初期位置を計測中...")
    sensors_prev, image = measure(base_url, settle_ms)
    print_sensors(sensors_prev, "初期")

    img_name = "step_000_initial.jpg"
    (output_dir / img_name).write_bytes(image)
    print(f"  画像保存: {img_name} ({len(image)} bytes)")
    print()

    initial_front = sensors_prev["front_mm"]
    cumulative = 0

    rows = []
    rows.append({
        "step": 0,
        "timestamp": datetime.now().isoformat(),
        "front_mm": sensors_prev["front_mm"],
        "right_mm": sensors_prev["right_mm"],
        "rear_mm": sensors_prev["rear_mm"],
        "left_mm": sensors_prev["left_mm"],
        "delta_front_mm": 0,
        "cumulative_front_mm": 0,
        "speed": 0,
        "duration_ms": 0,
        "image_file": img_name,
    })

    # ── Step 1..N: 後退 → 計測 ──
    try:
        for step in range(1, steps + 1):
            print(f"Step {step}/{steps}: 後退中 (speed={speed}, {duration_ms}ms)...")
            move_backward(base_url, speed, duration_ms)

            print(f"Step {step}/{steps}: 計測中...")
            sensors_now, image = measure(base_url, settle_ms)
            print_sensors(sensors_now, f"Step {step}")

            delta = sensors_now["front_mm"] - sensors_prev["front_mm"]
            cumulative = sensors_now["front_mm"] - initial_front
            print(f"  -> 前方距離変化: {delta:+d}mm (累計: {cumulative:+d}mm)")

            img_name = f"step_{step:03d}.jpg"
            (output_dir / img_name).write_bytes(image)
            print(f"  画像保存: {img_name} ({len(image)} bytes)")
            print()

            rows.append({
                "step": step,
                "timestamp": datetime.now().isoformat(),
                "front_mm": sensors_now["front_mm"],
                "right_mm": sensors_now["right_mm"],
                "rear_mm": sensors_now["rear_mm"],
                "left_mm": sensors_now["left_mm"],
                "delta_front_mm": delta,
                "cumulative_front_mm": cumulative,
                "speed": speed,
                "duration_ms": duration_ms,
                "image_file": img_name,
            })

            sensors_prev = sensors_now

    except KeyboardInterrupt:
        print("\n中断されました。モーターを停止します...")
        try:
            emergency_stop(base_url)
        except Exception:
            pass
    except Exception as e:
        print(f"\nERROR: {e}")
        print("モーターを停止します...")
        try:
            emergency_stop(base_url)
        except Exception:
            pass
        raise

    # ── CSVログ出力 ──
    with open(csv_path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)
    print(f"CSVログ保存: {csv_path}")

    # ── サマリー ──
    print()
    print("=" * 70)
    print("キャリブレーション結果サマリー")
    print("=" * 70)
    print(f"{'Step':>5} | {'Front(mm)':>10} | {'Delta(mm)':>10} | {'累計(mm)':>10} | {'画像'}")
    print("-" * 70)
    for row in rows:
        print(
            f"{row['step']:>5} | "
            f"{row['front_mm']:>10} | "
            f"{row['delta_front_mm']:>+10} | "
            f"{row['cumulative_front_mm']:>+10} | "
            f"{row['image_file']}"
        )
    print("-" * 70)

    if len(rows) > 1:
        deltas = [r["delta_front_mm"] for r in rows[1:]]
        avg_delta = sum(deltas) / len(deltas)
        print(f"1ステップ平均移動距離: {avg_delta:.1f}mm")
        print(f"  (speed={speed}, duration={duration_ms}ms)")
    print()


def main():
    parser = argparse.ArgumentParser(
        description="MOVA ロボット距離キャリブレーション: 後退移動とToF/画像の対応を記録"
    )
    parser.add_argument(
        "--host", required=True,
        help="ロボットのIPアドレスまたはホスト名 (例: 192.168.1.100)"
    )
    parser.add_argument(
        "--port", type=int, default=80,
        help="HTTPポート番号 (デフォルト: 80)"
    )
    parser.add_argument(
        "--speed", type=int, default=1024,
        help="後退モーター速度 0-4095 (デフォルト: 1024 = 約25%%)"
    )
    parser.add_argument(
        "--duration", type=int, default=500,
        help="1ステップの移動時間 ms (デフォルト: 500)"
    )
    parser.add_argument(
        "--steps", type=int, default=5,
        help="繰り返しステップ数 (デフォルト: 5)"
    )
    parser.add_argument(
        "--settle", type=int, default=500,
        help="計測前の静定待ち ms (デフォルト: 500)"
    )
    parser.add_argument(
        "--output", type=str, default=None,
        help="出力ディレクトリ (デフォルト: calibration_out/<timestamp>)"
    )
    args = parser.parse_args()

    base_url = f"http://{args.host}:{args.port}"

    if args.output:
        output_dir = Path(args.output)
    else:
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        output_dir = Path("calibration_out") / timestamp

    run_calibration(
        base_url=base_url,
        output_dir=output_dir,
        speed=args.speed,
        duration_ms=args.duration,
        steps=args.steps,
        settle_ms=args.settle,
    )


if __name__ == "__main__":
    main()
