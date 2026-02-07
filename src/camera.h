#ifndef MOVA_CAMERA_H
#define MOVA_CAMERA_H

#include <cstddef>
#include <cstdint>

namespace mova {

bool cameraInit();
bool cameraIsInitialized();

// JPEG キャプチャ。malloc で確保されたバッファを返す (呼び出し側が free する)
// quality: 10-63 (0 = デフォルト CAM_DEFAULT_QUALITY)
uint8_t* cameraCaptureJpeg(size_t* outLen, uint8_t quality = 0);

bool cameraStreamServerStart();

}  // namespace mova

#endif  // MOVA_CAMERA_H
