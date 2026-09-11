#ifndef BEEP_CONFIG_H
#define BEEP_CONFIG_H

#include <unistd.h>

//bool beep_config_main_write(const char *attr, const char *value);
//ssize_t beep_config_main_read_safe(const char *attr, char *buf, size_t buf_sz);
//int beep_config_main_get_int(const char *attr, int default_value);

size_t beep_config_data_read(const char *attr, char *buf, size_t buf_sz);
size_t beep_config_data_get_int(const char *attr, int default_value);
bool beep_config_data_write(const char *attr, const char* value);
size_t beep_config_device_read(const char *attr, char *buf, size_t buf_sz);
size_t beep_config_static_read(const char *attr, char *buf, size_t buf_sz);
size_t beep_config_devel_read(const char *attr, char *buf, size_t buf_sz);
size_t beep_config_devel_get_int(const char *attr, int default_value);

// App config API
typedef struct _app_config *AppConfig;

AppConfig beep_config_app_load(const char *app_id);
void beep_config_app_free(AppConfig app);
const char *beep_config_app_get_id(AppConfig app);
const char *beep_config_app_get_display_name(AppConfig app);
const char *beep_config_app_get_path(AppConfig app);
const char *beep_config_app_get_lib_path(AppConfig app);
const char *beep_config_app_get_dial_extra(AppConfig app);

#endif  // BEEP_CONFIG_H
