#ifndef MOVA_WEB_SERVER_H
#define MOVA_WEB_SERVER_H

namespace mova {

class MOVAWebServer {
public:
    bool begin();
    void stop();

private:
    bool running_ = false;
};

}  // namespace mova

#endif  // MOVA_WEB_SERVER_H
