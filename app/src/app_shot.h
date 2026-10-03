#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Posts the frame on screen to the payload as a BMP (in the headset: detail 1
 * = the left eye at half size, else both eyes small). 0 on success. */
int app_shot_post(int detail);

#ifdef __cplusplus
}
#endif
