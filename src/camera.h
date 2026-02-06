#ifndef MOVA_CAMERA_H
#define MOVA_CAMERA_H

#include <cstdint>
#include <cstddef>

namespace mova {

bool cameraInit();
uint8_t* cameraCaptureJpeg(size_t* outLen);
bool cameraStreamServerStart();
void taskCameraStream(void* param);

}  // namespace mova

#endif  // MOVA_CAMERA_H
