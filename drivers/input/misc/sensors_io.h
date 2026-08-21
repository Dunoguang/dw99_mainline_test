/*
*
* (C) Copyright 2008 
* MediaTek <www.mediatek.com>
*
* Sensors IO command file for MT6516
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
#ifndef SENSORS_IO_H
#define SENSORS_IO_H
		  
#include <linux/ioctl.h>	  


#define ALSPS							0X84
#define ALSPS_SET_PS_MODE					_IOW(ALSPS, 0x01, int)
#define ALSPS_GET_PS_MODE					_IOR(ALSPS, 0x02, int)
#define ALSPS_GET_PS_DATA					_IOR(ALSPS, 0x03, int)
#define ALSPS_GET_PS_RAW_DATA				_IOR(ALSPS, 0x04, int)
#define ALSPS_GET_PS_RAW_DATA_H				_IOR(ALSPS, 0x05, int)
#define ALSPS_GET_PS_RAW_DATA_L				_IOR(ALSPS, 0x06, int)



#define ALSPS_SET_ALS_MODE					_IOW(ALSPS, 0x07, int)
#define ALSPS_GET_ALS_MODE					_IOR(ALSPS, 0x08, int)
#define ALSPS_GET_ALS_DATA					_IOR(ALSPS, 0x09, int)
#define ALSPS_GET_ALS_RAW_DATA           	_IOR(ALSPS, 0x0a, int)



#endif

