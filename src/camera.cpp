#include "camera.h"

namespace mova {

bool cameraInit() {
    // TODO: Phase 4 - Configure esp_camera with Core S3 pin mapping
    return false;
}

uint8_t* cameraCaptureJpeg(size_t* outLen) {
    // TODO: Phase 4 - Capture JPEG frame, return buffer pointer
    (void)outLen;
    return nullptr;
}

bool cameraStreamServerStart() {
    // TODO: Phase 4 - Start MJPEG stream server on STREAM_PORT
    return false;
}

void taskCameraStream(void* param) {
    // TODO: Phase 4 - FreeRTOS task: continuous MJPEG streaming
    (void)param;
}

}  // namespace mova
