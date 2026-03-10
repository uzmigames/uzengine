#pragma once

#ifdef __cplusplus
extern "C" {
#endif

const char* uz_runtime_version(void);
const char* uz_runtime_backend(void);
int uz_runtime_init(void);
void uz_runtime_shutdown(void);

#ifdef __cplusplus
}
#endif
