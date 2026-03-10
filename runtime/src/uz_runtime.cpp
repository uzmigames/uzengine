#include "uz_runtime.h"

extern "C" {

const char* uz_runtime_version(void) {
    return "0.2.0";
}

const char* uz_runtime_backend(void) {
    return "uzengine";
}

int uz_runtime_init(void) {
    return 1;
}

void uz_runtime_shutdown(void) {
}

} // extern "C"
