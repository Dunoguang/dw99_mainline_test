/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_ZYT_INFO_H
#define _LINUX_ZYT_INFO_H


void ctp_lock_mutex(void);
void ctp_unlock_mutex(void);
int tp_device_id(int id);
int CDC_Gsensor_Device_Id(int id);
int CDC_Plsensor_Device_Id(int id);
void zyt_info(char *content);
void zyt_info_s(char *c1);
void zyt_info_s2(char *c1, char *c2);
void zyt_info_sx(char *c1, int x);
char *zyt_battery_info(void);
char *zyt_get_lcm_info(void);
char *get_sensor_info(int sensor_id);

#endif
