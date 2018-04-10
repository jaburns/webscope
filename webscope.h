#ifndef __WEBSCOPE_H
#define __WEBSCOPE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

extern void webscope_open(uint16_t port);
extern void webscope_close();

extern void webscope_update();
extern float webscope_value(const char *label, float default_value, float min, float max);

#ifdef __cplusplus
}
#endif

#endif//__WEBSCOPE_H