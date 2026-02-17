#!/bin/bash
# send_audio.sh - /tmp/mova_tts.wav のPCMをBase64化してMOVA /command に送信する
# Usage: send_audio.sh <HOST> <JSON_TEMPLATE>
# JSON_TEMPLATE 内の AUDIO_DATA_PLACEHOLDER が Base64 PCM データに置換される
# Example: send_audio.sh 192.168.0.167 '{"audio":{"sample_rate":16000,"bits":16,"channels":1,"data":"AUDIO_DATA_PLACEHOLDER"},"emoji":"😎"}'
set -e
HOST="$1"
JSON="$2"
PCM=$(tail -c +45 /tmp/mova_tts.wav | base64 -w 0)
echo -n "${JSON//AUDIO_DATA_PLACEHOLDER/$PCM}" | curl -s -X POST "http://$HOST/command" -H 'Content-Type: application/json' -d @-
