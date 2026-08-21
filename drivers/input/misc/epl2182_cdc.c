/* drivers/i2c/chips/epl2182.c - light and proxmity sensors driver
 * Copyright (C) 2011 ELAN Corporation.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/hrtimer.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/earlysuspend.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/irq.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <asm/mach-types.h>
#include <asm/setup.h>
#include <linux/wakelock.h>
#include <linux/jiffies.h>
#include <linux/regulator/consumer.h>
#include <linux/platform_device.h>
#include <linux/poll.h>

#include <linux/of_gpio.h>

#define CONFIG_ZYT_PLSENSOR_COMPATIBLE
#define CONFIG_ZYT_SUPPORT


#include "elan_interface.h"

#include "sensors_io.h"
#include <soc/sprd/board.h>

extern int CDC_Plsensor_Device_Id(int);

// Alex.shi Self-Defined Macro
// Alex.shi join als_input_dev and ps_input_dev together  
#define EPL_SINGLE_INPUT_DEVICE

/******************************************************************************
 * configuration
*******************************************************************************/
#define EPL_DEV_NAME "epl2182_pls"
#define DRIVER_VERSION "v1.0.0"

#define PS_INTERRUPT_MODE		1		// 0 is polling mode, 1 is interrupt mode

#define P_SENSOR_LTHD			120		//100
#define P_SENSOR_HTHD			170		//500
#define HS_ENABLE    1   //1 is enable, 0 is disable    //heart rate
#define DYN_ENABLE      1

//platform select
#define S5PV210 0
#define SPREAD  1
/******************************************************************************
 * configuration
*******************************************************************************/

typedef enum
{
    CMC_MODE_ALS    = 0x00,
    CMC_MODE_PS     = 0x10,
} CMC_MODE;

#define TXBYTES				2
#define RXBYTES				2

//#define PS_DELAY			50
//#define ALS_DELAY			55


#define PACKAGE_SIZE		2
#define I2C_RETRY_COUNT		10

#define PS_INTT				4
#define ALS_INTT			6	//5-8


static int set_psensor_intr_threshold(uint16_t low_thd, uint16_t high_thd);

#if PS_INTERRUPT_MODE || HS_ENABLE
//#define EPL_INTR_PIN xx
static void epl_sensor_irq_do_work(struct work_struct *work);
static DECLARE_WORK(epl_sensor_irq_work, epl_sensor_irq_do_work);
#endif


static void polling_do_work(struct work_struct *work);
static DECLARE_DELAYED_WORK(polling_work, polling_do_work);

#define LUX_PER_COUNT			440		// 660 = 1.1*0.6*1000


#define PS_DELAY			50
#define ALS_DELAY			100



struct _epl_ps_als_factory
{
    bool cal_file_exist;
    bool cal_finished;
    u16 ps_cal_h;
    u16 ps_cal_l;
    char ps_s1[16];
    char ps_s2[16];
};

/* primitive raw data from I2C */
typedef struct _epl_raw_data
{
    u8 raw_bytes[PACKAGE_SIZE];
    u16 ps_state;
    u16 ps_int_state;
    u16 ps_ch0_raw;
    u16 ps_ch1_raw;
    u16 als_ch1_raw;
#if HS_ENABLE
    u16 renvo;
    u16 hs_data[200];
#endif
#if DYN_ENABLE
	u16 ps_min_raw;
	u16 ps_sta;
	u16 ps_dyn_high;
	u16 ps_dyn_low;
	bool ps_dny_ini_lock;
#endif
    struct _epl_ps_als_factory ps_als_factory;
} epl_raw_data;

struct elan_epl_data
{
    struct i2c_client *client;
    struct input_dev *als_input_dev;
    struct input_dev *ps_input_dev;
    struct workqueue_struct *epl_wq;
    struct early_suspend early_suspend;

    int ps_threshold_high;
    int ps_threshold_low;

    int intr_pin;
    int (*power)(int on);

    int ps_opened;
    int als_opened;

    int enable_pflag;
    int enable_lflag;
    int enable_hflag;
    int read_flag;
    int irq;
    int         hs_intt;
    int polling_mode_hs;
} ;

static DECLARE_WAIT_QUEUE_HEAD(ps_waitqueue);
static DECLARE_WAIT_QUEUE_HEAD(ls_waitqueue);


static int ps_data_changed;
static int ls_data_changed;


static struct wake_lock ps_lock;
struct elan_epl_data *epl_data;
static epl_raw_data	gRawData;

#ifdef EPL_SINGLE_INPUT_DEVICE
static const char ElanPS_Name[] = "epl2182_pls";
#else
static const char ElanPsensorName[] = "proximity";
static const char ElanALsensorName[] = "light";
#endif

static int psensor_mode_suspend = 0;

// for heart rate
#if HS_ENABLE
#define HS_POLLING_MODE     0                   // 1 is polling mode, 0 is interrupt mode
#define HS_DELAY 			30
#define PS_DRIVE				EPL_DRIVE_120MA
static int HS_INTT_CENTER = EPL_INTT_PS_272; //EPL_INTT_PS_48
static struct mutex sensor_mutex;
static bool change_int_time = false;
static int hs_count=0;
static int hs_idx=0;
static int show_hs_raws_flag=0;
static int hs_als_flag=0;
#endif

#if DYN_ENABLE
#define DYN_H_OFFSET 	 	260
#define DYN_L_OFFSET		600
#define DYN_PS_CONDITION	30000
#endif

#define LOG_TAG 					"[EPL2182] "
//#define elan_debug
#ifdef elan_debug
#define LOG_FUN(f)			        printk(KERN_INFO LOG_TAG"%s\n", __FUNCTION__)
#define LOG_INFO(fmt, args...)		printk(KERN_INFO LOG_TAG fmt, ##args)
#else
#define LOG_FUN(...)
#define LOG_INFO(...)
#endif
#define LOG_ERR(fmt, args...)		printk(KERN_ERR  LOG_TAG"%s %d : "fmt, __FUNCTION__, __LINE__, ##args)

/*
//====================I2C write operation===============//
//regaddr: ELAN Register Address.
//bytecount: How many bytes to be written to register via i2c bus.
//txbyte: I2C bus transmit byte(s). Single byte(0X01) transmit only slave address.
//data: setting value.
//
// Example: If you want to write single byte to 0x1D register address, show below
//	      elan_sensor_I2C_Write(client,0x1D,0x01,0X02,0xff);
//
*/
static int elan_sensor_I2C_Write(struct i2c_client *client, uint8_t regaddr, uint8_t bytecount, uint8_t txbyte, uint8_t data)
{
    uint8_t buffer[2];
    int ret = 0;
    int retry,val;
    struct elan_epl_data *epld = epl_data;

    buffer[0] = (regaddr<<3) | bytecount ;
    buffer[1] = data;

    for(retry = 0; retry < I2C_RETRY_COUNT; retry++)
    {
        ret = i2c_master_send(client, buffer, txbyte);

        if (ret == txbyte)
        {
            break;
        }

        val = gpio_get_value(epld->intr_pin);

        LOG_INFO("INTERRUPT GPIO val = %d\n",  val);

        msleep(10);
    }

    if(retry>=I2C_RETRY_COUNT)
    {
        LOG_ERR(KERN_ERR "i2c write retry over %d\n", I2C_RETRY_COUNT);
        return -EINVAL;
    }

    return ret;
}

static int elan_sensor_I2C_Read(struct i2c_client *client)
{
    uint8_t buffer[RXBYTES];
    int ret = 0, i =0;
    int retry,val;
    struct elan_epl_data *epld = epl_data;

    for(retry = 0; retry < I2C_RETRY_COUNT; retry++)
    {
        ret = i2c_master_recv(client, buffer, RXBYTES);

        if (ret == RXBYTES)
            break;

        val = gpio_get_value(epld->intr_pin);

        LOG_INFO("INTERRUPT GPIO val = %d\n", val);

        msleep(10);
    }

    if(retry>=I2C_RETRY_COUNT)
    {
        LOG_ERR("i2c read retry over %d\n", I2C_RETRY_COUNT);
        return -EINVAL;
    }

    for(i=0; i<PACKAGE_SIZE; i++)
    {
        gRawData.raw_bytes[i] = buffer[i];
    }

    return ret;
}

static int elan_sensor_I2C_Read_long(struct i2c_client *client, int bytecount)
{
    uint8_t buffer[bytecount];
    int ret = 0, i =0;
    int retry;

    for(retry = 0; retry < I2C_RETRY_COUNT; retry++)
    {
        ret = i2c_master_recv(client, buffer, bytecount);

        if (ret == bytecount)
            break;

        LOG_ERR("i2c read error,RXBYTES %d\r\n",ret);
        mdelay(10);
    }

    if(retry>=I2C_RETRY_COUNT)
    {
        LOG_ERR("i2c read retry over %d\n", I2C_RETRY_COUNT);
        return -EINVAL;
    }

    for(i=0; i<bytecount; i++)
        gRawData.raw_bytes[i] = buffer[i];

    return ret;
}

static void elan_sensor_restart_work(void)
{
    struct elan_epl_data *epld = epl_data;

    cancel_delayed_work(&polling_work);
    queue_delayed_work(epld->epl_wq, &polling_work,msecs_to_jiffies(50));

}

/*----------------------------------------------------------------------------*/

static int elan_calibration_atoi(char* s)
{
    int num=0,flag=0;
    int i=0;
    //printk("[ELAN] %s\n", __func__);
    for(i=0; i<=strlen(s); i++)
    {
        if(s[i] >= '0' && s[i] <= '9')
            num = num * 10 + s[i] -'0';
        else if(s[0] == '-' && i==0)
            flag =1;
        else
            break;
    }
    if(flag == 1)
        num = num * -1;
    return num;
}

static int elan_calibaration_read(struct elan_epl_data *epl_ps_data)
{
	struct file *fp_h;
	struct file *fp_l;
	mm_segment_t fs;
	loff_t pos;
	LOG_INFO("[ELAN] %s\n", __func__);

      //modify by ELAN Robert, checking calibration exist
	if(gRawData.ps_als_factory.cal_file_exist == 1)
	{
		fp_h = filp_open("/data/data/com.eminent.ps.calibration/h-threshold.dat", O_RDWR, 0777);
		if (IS_ERR(fp_h))
		{
			LOG_ERR("[ELAN]create file_h error\n");
			gRawData.ps_als_factory.cal_file_exist = 0;
		}

		fp_l = filp_open("/data/data/com.eminent.ps.calibration/l-threshold.dat", O_RDWR, 0777);

		if (IS_ERR(fp_l))
		{
			LOG_ERR("[ELAN]create file_l error\n");
			gRawData.ps_als_factory.cal_file_exist = 0;
		}
	}

	//modify by ELAN Robert, open calibration and read high / low threshold to hw structure. if open file fail, high / low threshold will use default.
	if(gRawData.ps_als_factory.cal_file_exist == 1)
	{
        int read_ret = 0;
		fs = get_fs();
		set_fs(KERNEL_DS);
		pos = 0;
		//gRawData.als_factory.s1 = {NULL, NULL, NULL, NULL, NULL};
		read_ret = vfs_read(fp_h, gRawData.ps_als_factory.ps_s1, sizeof(gRawData.ps_als_factory.ps_s1), &pos);
		gRawData.ps_als_factory.ps_s1[read_ret] = '\0';

		pos = 0;
		//gRawData.als_factory.s2 = {NULL, NULL, NULL, NULL, NULL};
		read_ret = vfs_read(fp_l, gRawData.ps_als_factory.ps_s2, sizeof(gRawData.ps_als_factory.ps_s2), &pos);
		gRawData.ps_als_factory.ps_s2[read_ret] = '\0';


		filp_close(fp_h, NULL);
		filp_close(fp_l, NULL);
		set_fs(fs);

		gRawData.ps_als_factory.ps_cal_h = elan_calibration_atoi(gRawData.ps_als_factory.ps_s1);
		gRawData.ps_als_factory.ps_cal_l = elan_calibration_atoi(gRawData.ps_als_factory.ps_s2);
		epl_ps_data->ps_threshold_high = gRawData.ps_als_factory.ps_cal_h;
		epl_ps_data->ps_threshold_low = gRawData.ps_als_factory.ps_cal_l;
		LOG_INFO("[ELAN] read cal_h: %d , cal_l : %d\n", gRawData.ps_als_factory.ps_cal_h,gRawData.ps_als_factory.ps_cal_l);
	}

	gRawData.ps_als_factory.cal_finished = 1;
	return 0;
}

#if HS_ENABLE
static void elan_sensor_hs_enable(struct elan_epl_data *epld, bool interrupt, bool full_enable)
{
    int ret;
    uint8_t regdata = 0;
    struct i2c_client *client = epld->client;
    if(full_enable)
    {
        regdata = PS_DRIVE | (interrupt? EPL_INT_FRAME_ENABLE : EPL_INT_DISABLE);
        ret = elan_sensor_I2C_Write(client,REG_9,W_SINGLE_BYTE,0x02, regdata);

        regdata = EPL_SENSING_1_TIME | EPL_PS_MODE | EPL_L_GAIN | EPL_S_SENSING_MODE;
        ret = elan_sensor_I2C_Write(client,REG_0,W_SINGLE_BYTE,0X02,regdata);

        regdata = /*HS_INTT*/ epld->hs_intt<<4 | EPL_PST_1_TIME | EPL_12BIT_ADC;
        ret = elan_sensor_I2C_Write(client,REG_1,W_SINGLE_BYTE,0X02,regdata);

        ret = elan_sensor_I2C_Write(client,REG_7,W_SINGLE_BYTE,0X02,EPL_C_RESET);


    }

    ret = elan_sensor_I2C_Write(client,REG_7,W_SINGLE_BYTE,0x02,EPL_C_START_RUN);

    if(epld->polling_mode_hs == 1){
        msleep(HS_DELAY);
    }

}
#endif

#if DYN_ENABLE
static void dyn_ps_cal(struct elan_epl_data *epld)
{
	if((gRawData.ps_ch1_raw < gRawData.ps_min_raw)
	&& (gRawData.ps_sta != 1)
	&& (gRawData.ps_ch0_raw <= DYN_PS_CONDITION))
	{
		gRawData.ps_min_raw = gRawData.ps_ch1_raw;
		epld->ps_threshold_low = gRawData.ps_ch1_raw + DYN_L_OFFSET;
		epld->ps_threshold_high = gRawData.ps_ch1_raw + DYN_H_OFFSET;
		set_psensor_intr_threshold(epld->ps_threshold_low, epld->ps_threshold_high);
		LOG_INFO("dyn ps raw = %d, min = %d, ch0 = %d\n dyn l_thrd = %d, h_thrd = %d, ps_state = %d",
		gRawData.ps_ch1_raw, gRawData.ps_min_raw, gRawData.ps_ch0_raw,epld->ps_threshold_low,epld->ps_threshold_high, gRawData.ps_state);
	}
}
#endif

static int elan_sensor_psensor_enable(struct elan_epl_data *epld)
{
    int ret;
    uint8_t regdata = 0;
    struct i2c_client *client = epld->client;

    LOG_INFO("--- Proximity sensor Enable --- \n");

    //disable_irq(epld->irq);
    ret = elan_sensor_I2C_Write(client,REG_9,W_SINGLE_BYTE,0x02,EPL_INT_DISABLE);

    regdata = EPL_SENSING_8_TIME | EPL_PS_MODE | EPL_L_GAIN ;
    regdata = regdata | (PS_INTERRUPT_MODE ? EPL_C_SENSING_MODE : EPL_S_SENSING_MODE);
    ret = elan_sensor_I2C_Write(client,REG_0,W_SINGLE_BYTE,0X02,regdata);

    regdata = PS_INTT<<4 | EPL_PST_1_TIME | EPL_10BIT_ADC;
    ret = elan_sensor_I2C_Write(client,REG_1,W_SINGLE_BYTE,0X02,regdata);

    //set_psensor_intr_threshold(P_SENSOR_LTHD ,P_SENSOR_HTHD);
    if(gRawData.ps_als_factory.cal_finished == 0 &&  gRawData.ps_als_factory.cal_file_exist ==1)
	    ret=elan_calibaration_read(epld);

    LOG_INFO("[%s] cal_finished = %d, cal_file_exist = %d\n", __FUNCTION__, gRawData.ps_als_factory.cal_finished , gRawData.ps_als_factory.cal_file_exist);
#if !DYN_ENABLE
    set_psensor_intr_threshold(epld->ps_threshold_low,epld->ps_threshold_high);
#endif


    ret = elan_sensor_I2C_Write(client,REG_7,W_SINGLE_BYTE,0X02,EPL_C_RESET);
    ret = elan_sensor_I2C_Write(client,REG_7,W_SINGLE_BYTE,0x02,EPL_C_START_RUN);
    msleep(PS_DELAY);


    elan_sensor_I2C_Write(client,REG_13,R_SINGLE_BYTE,0x01,0);
    elan_sensor_I2C_Read(client);
    gRawData.ps_state= !((gRawData.raw_bytes[0]&0x04)>>2);
    gRawData.ps_sta = ((gRawData.raw_bytes[0]&0x02)>>1);

    elan_sensor_I2C_Write(client,REG_14,R_TWO_BYTE,0x01,0x00);
    elan_sensor_I2C_Read(client);
	gRawData.ps_ch0_raw = ((gRawData.raw_bytes[1]<<8) | gRawData.raw_bytes[0]);

    elan_sensor_I2C_Write(client,REG_16,R_TWO_BYTE,0x01,0x00);
    elan_sensor_I2C_Read(client);
	gRawData.ps_ch1_raw = ((gRawData.raw_bytes[1]<<8) | gRawData.raw_bytes[0]);

#if DYN_ENABLE
        dyn_ps_cal(epld);
		LOG_INFO("dyn k ps raw = %d, ch0 = %d\n, ps_state = %d \r\n", gRawData.ps_ch1_raw, gRawData.ps_ch0_raw, gRawData.ps_state);
#endif

#if PS_INTERRUPT_MODE
    if(PS_INTERRUPT_MODE == 1)
    {
        if(gRawData.ps_state!= gRawData.ps_int_state)
        {
            elan_sensor_I2C_Write(client,REG_9,W_SINGLE_BYTE,0x02,EPL_INT_FRAME_ENABLE);
        }
        else
        {
            elan_sensor_I2C_Write(client,REG_9,W_SINGLE_BYTE,0x02,EPL_INT_ACTIVE_LOW);
        }

    }
#endif

    if (ret != 0x02)
    {
        LOG_INFO("P-sensor i2c err\n");
    }

    return ret;
}

static int elan_sensor_lsensor_enable(struct elan_epl_data *epld)
{
    int ret;
    uint8_t regdata = 0;
    struct i2c_client *client = epld->client;

    LOG_INFO("--- ALS sensor Enable --- \n");

    //disable_irq(epld->irq);
    regdata = EPL_INT_DISABLE;
    ret = elan_sensor_I2C_Write(client,REG_9,W_SINGLE_BYTE,0x02, regdata);

    regdata = EPL_S_SENSING_MODE | EPL_SENSING_8_TIME | EPL_ALS_MODE | EPL_AUTO_GAIN;
    ret = elan_sensor_I2C_Write(client,REG_0,W_SINGLE_BYTE,0X02,regdata);

    regdata = ALS_INTT<<4 | EPL_PST_1_TIME | EPL_10BIT_ADC;
    ret = elan_sensor_I2C_Write(client,REG_1,W_SINGLE_BYTE,0X02,regdata);

    ret = elan_sensor_I2C_Write(client,REG_10,W_SINGLE_BYTE,0x02, EPL_GO_MID);
    ret = elan_sensor_I2C_Write(client,REG_11,W_SINGLE_BYTE,0x02, EPL_GO_LOW);

    ret = elan_sensor_I2C_Write(client,REG_7,W_SINGLE_BYTE,0X02,EPL_C_RESET);
    ret = elan_sensor_I2C_Write(client,REG_7,W_SINGLE_BYTE,0x02,EPL_C_START_RUN);

    msleep(ALS_DELAY);

    if(ret != 0x02)
    {
        LOG_INFO(" ALS-sensor i2c err\n");
    }

    return ret;

}

#if HS_ENABLE
static void elan_sensor_read_hs(void)
{

    struct elan_epl_data *epld = epl_data;
    struct i2c_client *client = epld->client;
    int max_frame = 200;
    int idx = hs_idx+hs_count;
    u16 data;
    mutex_lock(&sensor_mutex);

    elan_sensor_I2C_Write(client,REG_16,R_TWO_BYTE,0x01,0x00);
    elan_sensor_I2C_Read_long(client, 2);

    data=(gRawData.raw_bytes[1]<<8)|gRawData.raw_bytes[0];
    if(data>60800&& /*HS_INTT*/ epld->hs_intt>HS_INTT_CENTER-3)
    {
        /*HS_INTT*/ epld->hs_intt--;
        change_int_time=true;
    }
    else if(data>6400 && data <25600 && /*HS_INTT*/ epld->hs_intt<HS_INTT_CENTER+3)
    {
        /*HS_INTT*/ epld->hs_intt++;
        change_int_time=true;
    }
    else
    {
        change_int_time=false;

        if(idx>=max_frame)
            idx-=max_frame;

        gRawData.hs_data[idx] = data;

        if(hs_count>=max_frame)
        {
            hs_idx++;
            if(hs_idx>=max_frame)
                hs_idx=0;
        }

        hs_count++;
        if(hs_count>=max_frame)
            hs_count=max_frame;
    }
    mutex_unlock(&sensor_mutex);

}
#endif


/*
//====================elan_epl_ps_poll_rawdata===============//
//polling method for proximity sensor detect. Report proximity sensor raw data.
//Report "ABS_DISTANCE" event to HAL layer.
//Variable "value" 0 and 1 to represent which distance from psensor to target(human's face..etc).
//value: 0 represent near.
//value: 1 represent far.
*/
static void elan_epl_ps_poll_rawdata(void)
{
    struct elan_epl_data *epld = epl_data;
    struct i2c_client *client = epld->client;

    //elan_sensor_I2C_Write(epld->client,REG_7,W_SINGLE_BYTE,0x02,EPL_DATA_LOCK);

    elan_sensor_I2C_Write(client,REG_13,R_SINGLE_BYTE,0x01,0);
    elan_sensor_I2C_Read(client);
    gRawData.ps_state= !((gRawData.raw_bytes[0]&0x04)>>2);

    elan_sensor_I2C_Write(client,REG_16,R_TWO_BYTE,0x01,0x00);
    elan_sensor_I2C_Read(client);
    gRawData.ps_ch1_raw = (gRawData.raw_bytes[1]<<8) | gRawData.raw_bytes[0];

    //elan_sensor_I2C_Write(epld->client,REG_7,W_SINGLE_BYTE,0x02,EPL_DATA_UNLOCK);

    LOG_INFO("### ps_ch1_raw_data  (%d), value(%d) ###\n\n", gRawData.ps_ch1_raw, gRawData.ps_state);
	ps_data_changed = 1;
    input_report_abs(epld->ps_input_dev, ABS_DISTANCE, gRawData.ps_state);
    input_sync(epld->ps_input_dev);
}



static void elan_epl_als_rawdata(void)
{
    struct elan_epl_data *epld = epl_data;
    struct i2c_client *client = epld->client;

    uint32_t lux;

    elan_sensor_I2C_Write(client,REG_16,R_TWO_BYTE,0x01,0x00);
    elan_sensor_I2C_Read(client);
    gRawData.als_ch1_raw = (gRawData.raw_bytes[1]<<8) | gRawData.raw_bytes[0];

    lux =  (gRawData.als_ch1_raw* LUX_PER_COUNT) / 1000;
    if(lux>20000)
        lux=20000;

    LOG_INFO("-------------------  ALS raw = %d, lux = %d\n\n",  gRawData.als_ch1_raw,  lux);
	ls_data_changed = 1;
#if SPREAD
    input_report_abs(epld->ps_input_dev, ABS_MISC, lux);
    input_sync(epld->ps_input_dev);
#else
    input_report_abs(epld->als_input_dev, ABS_MISC, lux);
    input_sync(epld->als_input_dev);
#endif
}

/*
//====================set_psensor_intr_threshold===============//
//low_thd: The value is psensor interrupt low threshold.
//high_thd:	The value is psensor interrupt hihg threshold.
//When psensor rawdata > hihg_threshold, interrupt pin will be pulled low.
//After interrupt occur, psensor rawdata < low_threshold, interrupt pin will be pulled high.
*/
static int set_psensor_intr_threshold(uint16_t low_thd, uint16_t high_thd)
{
    int ret = 0;
    struct elan_epl_data *epld = epl_data;
    struct i2c_client *client = epld->client;

    uint8_t high_msb ,high_lsb, low_msb, low_lsb;

    high_msb = (uint8_t) (high_thd >> 8);
    high_lsb = (uint8_t) (high_thd & 0x00ff);
    low_msb  = (uint8_t) (low_thd >> 8);
    low_lsb  = (uint8_t) (low_thd & 0x00ff);

    elan_sensor_I2C_Write(client,REG_2,W_SINGLE_BYTE,0x02,high_lsb);
    elan_sensor_I2C_Write(client,REG_3,W_SINGLE_BYTE,0x02,high_msb);
    elan_sensor_I2C_Write(client,REG_4,W_SINGLE_BYTE,0x02,low_lsb);
    elan_sensor_I2C_Write(client,REG_5,W_SINGLE_BYTE,0x02,low_msb);

    LOG_INFO("[%s]: L(%d), H(%d) \r\n", __FUNCTION__, low_thd, high_thd);
    return ret;
}

#if PS_INTERRUPT_MODE || HS_ENABLE
static void epl_sensor_irq_do_work(struct work_struct *work)
{
    struct elan_epl_data *epld = epl_data;
    struct i2c_client *client = epld->client;
    int mode = 0;
    LOG_FUN();

    //elan_sensor_I2C_Write(epld->client,REG_7,W_SINGLE_BYTE,0x02,EPL_DATA_LOCK);

    elan_sensor_I2C_Write(client,REG_13,R_SINGLE_BYTE,0x01,0);
    elan_sensor_I2C_Read(client);
    mode = gRawData.raw_bytes[0]&(3<<4);
#if HS_ENABLE
    if(epld->enable_hflag == 1)
    {
        elan_sensor_read_hs();
        elan_sensor_hs_enable(epld, true, change_int_time);
    }
    else if(mode==CMC_MODE_PS && epld->enable_pflag)
#else
    if(mode==CMC_MODE_PS && epld->enable_pflag)
#endif
    // 0x10 is ps mode
    {
        gRawData.ps_int_state= !((gRawData.raw_bytes[0]&0x04)>>2);
        elan_epl_ps_poll_rawdata();

        elan_sensor_I2C_Write(client,REG_9,W_SINGLE_BYTE,0x02,EPL_INT_ACTIVE_LOW);
        elan_sensor_I2C_Write(client,REG_7,W_SINGLE_BYTE,0x02,EPL_DATA_UNLOCK);
    }


    enable_irq(epld->irq);
}

static irqreturn_t elan_sensor_irq_handler(int irqNo, void *handle)
{
    struct elan_epl_data *epld = (struct elan_epl_data*)handle;

    disable_irq_nosync(epld->irq);
    queue_work(epld->epl_wq, &epl_sensor_irq_work);

    return IRQ_HANDLED;
}
#endif

static void polling_do_work(struct work_struct *work)
{
    struct elan_epl_data *epld = epl_data;
    struct i2c_client *client = epld->client;

    bool isInterleaving = epld->enable_pflag==1 && epld->enable_lflag==1;
    bool isAlsOnly = epld->enable_pflag==0 && epld->enable_lflag==1;
    bool isPsOnly = epld->enable_pflag==1 && epld->enable_lflag==0;
    bool enable_hs = epld->enable_hflag==1;
    cancel_delayed_work(&polling_work);

    LOG_INFO("enable_pflag = %d, enable_lflag = %d, enable_hs = %d\n", epld->enable_pflag, epld->enable_lflag, enable_hs);
    if(isInterleaving || isAlsOnly || (isPsOnly && PS_INTERRUPT_MODE==0) || DYN_ENABLE)
    {
        queue_delayed_work(epld->epl_wq, &polling_work,msecs_to_jiffies(ALS_DELAY+2*PS_DELAY+30));
    }

    if(isAlsOnly || isInterleaving)
    {
        elan_sensor_lsensor_enable(epld);
        elan_epl_als_rawdata();

        if(isInterleaving)
        {
            elan_sensor_psensor_enable(epld);
            if(PS_INTERRUPT_MODE == 0)
            {
                elan_epl_ps_poll_rawdata();
            }

        }

    }
    else if(isPsOnly)
    {
        elan_sensor_psensor_enable(epld);

        if(PS_INTERRUPT_MODE)
        {
            // do nothing
        }
        else
        {
            elan_epl_ps_poll_rawdata();
        }
    }
#if HS_ENABLE
    else if(enable_hs)
    {
        if (epld->polling_mode_hs==0)
        {
            elan_sensor_hs_enable(epld, true, true);
        }
        else
        {
            elan_sensor_read_hs();
            elan_sensor_hs_enable(epld, false, true);
            queue_delayed_work(epld->epl_wq, &polling_work,msecs_to_jiffies(5));
        }
    }
#endif
    else
    {
        elan_sensor_I2C_Write(client,REG_9,W_SINGLE_BYTE,0x02,EPL_INT_DISABLE);
        elan_sensor_I2C_Write(client,REG_0,W_SINGLE_BYTE,0X02,EPL_S_SENSING_MODE);
        cancel_delayed_work(&polling_work);
    }
}

static ssize_t elan_ls_operationmode_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    uint16_t mode=0;
    struct elan_epl_data *epld = epl_data;
    LOG_FUN();

    sscanf(buf, "%hu",&mode);
    LOG_INFO("==>[operation mode]=%d\n", mode);

    if(mode == 0)
    {
        epld->enable_lflag = 0;
        epld->enable_pflag = 0;
    }
    else if(mode == 1)
    {
        epld->enable_lflag = 1;
        epld->enable_pflag = 0;
    }
    else if(mode == 2)
    {
        epld->enable_lflag = 0;
        epld->enable_pflag = 1;
    }
    else if(mode == 3)
    {
        epld->enable_lflag = 1;
        epld->enable_pflag = 1;
    }
    else
    {
        LOG_INFO("0: none\n1: als only\n2: ps only\n3: interleaving");
    }

    elan_sensor_restart_work();
    return count;
}

static ssize_t elan_ls_operationmode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct elan_epl_data *epld = epl_data;
    long *tmp = (long*)buf;
    uint16_t mode =0;
    LOG_FUN();

    if(  epld->enable_pflag==0 &&  epld->enable_lflag==0)
    {
        mode = 0;
    }
    else if(  epld->enable_pflag==0 &&  epld->enable_lflag==1)
    {
        mode = 1;
    }
    else if(  epld->enable_pflag==1 && epld->enable_lflag==0)
    {
        mode = 2;
    }
    else if(  epld->enable_pflag==1 && epld->enable_lflag==1)
    {
        mode = 3;
    }
    tmp[0] = mode;
    return sprintf(buf, "%d \n", mode);
}

static ssize_t elan_ls_status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct elan_epl_data *epld = epl_data;
    ssize_t len = 0;
#if 0
    u16 ch1;
    if(!epl_data)
    {
        LOG_INFO("epl_data is null!!\n");
        return 0;
    }
    elan_sensor_I2C_Write(epld->client,REG_7,W_SINGLE_BYTE,0x02,EPL_DATA_LOCK);

    elan_sensor_I2C_Write(epld->client,REG_16,R_TWO_BYTE,0x01,0x00);
    elan_sensor_I2C_Read(epld->client);
    ch1 = (gRawData.raw_bytes[1]<<8) | gRawData.raw_bytes[0];
    LOG_INFO("ch1 raw_data = %d\n", ch1);

    elan_sensor_I2C_Write(epld->client,REG_7,W_SINGLE_BYTE,0x02,EPL_DATA_UNLOCK);
    return sprintf(buf, "%d\n", ch1);
#else
    len += snprintf(buf+len, PAGE_SIZE-len, "chip is %s, ver is %s \n", EPL_DEV_NAME, DRIVER_VERSION);
    len += snprintf(buf+len, PAGE_SIZE-len, "als/ps int time is %d-%d\n",ALS_INTT, PS_INTT);
    len += snprintf(buf+len, PAGE_SIZE-len, "ch0 ch1 raw is %d-%d\n",gRawData.ps_ch0_raw, gRawData.ps_ch1_raw);
    len += snprintf(buf+len, PAGE_SIZE-len, "threshold is %d/%d\n",epld->ps_threshold_low, epld->ps_threshold_high);
    len += snprintf(buf+len, PAGE_SIZE-len, "heart int time: %d\n", epld->hs_intt);
#if DYN_ENABLE
    len += snprintf(buf+len, PAGE_SIZE-len, "ps dyn K: ch0=%d, ps raw=%d, ps_state=%d \n", gRawData.ps_ch0_raw, gRawData.ps_ch1_raw, gRawData.ps_state);
    len += snprintf(buf+len, PAGE_SIZE-len, "ps dyn K: threshold=%d/%d \n", epld->ps_threshold_low, epld->ps_threshold_high);
#endif
    return len;

#endif

}

#if HS_ENABLE
static ssize_t elan_sensor_show_renvo(struct device *dev, struct device_attribute *attr, char *buf)
{
    ssize_t len = 0;

    LOG_FUN();
    LOG_INFO("gRawData.renvo=0x%x \r\n", gRawData.renvo);

    len += snprintf(buf+len, PAGE_SIZE-len, "%x",gRawData.renvo);

    return len;
}

/*----------------------------------------------------------------------------*/
static ssize_t elan_sensor_store_hs_enable(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    uint16_t mode=0;
    struct elan_epl_data *epld = epl_data;
    bool enable_als = epld->enable_lflag==1;

    LOG_FUN();

    sscanf(buf, "%hu",&mode);


    if(mode){
        if(enable_als == true){
            epld->enable_lflag = 0;
            hs_als_flag = 1;
            if(epld->polling_mode_hs == 1)
                msleep(ALS_DELAY);
        }
        epld->enable_hflag = 1;
    }
    else{
        epld->enable_hflag = 0;
        if(epld->polling_mode_hs == 1)
                msleep(HS_DELAY);

        if(hs_als_flag == 1){
            epld->enable_lflag = 1;
            hs_als_flag = 0;

        }
    }
    if(mode)
    {
        //HS_INTT = EPL_INTT_PS_272;
        hs_idx=0;
        hs_count=0;
    }

    elan_sensor_restart_work();
    return count;
}

static ssize_t elan_sensor_show_hs_raws(struct device *dev, struct device_attribute *attr, char *buf)
{
    u16 *tmp = (u16*)buf;
    u16 length= hs_count;
    int byte_count=2+length*2;
    int i=0;
    int start = hs_idx;
    LOG_FUN();
    mutex_lock(&sensor_mutex);
    tmp[0]= length;
    if(length == 0){
        tmp[0] = 1;
        length = 1;
        show_hs_raws_flag = 1;
    }
    for(i=start; i<length; i++){
        if(show_hs_raws_flag == 1){
            tmp[i+1] = 0;
            show_hs_raws_flag = 0;
        }
        else{
            tmp[i+1] = gRawData.hs_data[i];
        }

    }

    hs_count=0;
    hs_idx=0;
    mutex_unlock(&sensor_mutex);

    return byte_count;
}

static ssize_t elan_sensor_store_hs_polling(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct elan_epl_data *epld = epl_data;
    LOG_FUN();

    sscanf(buf, "%d",&epld->polling_mode_hs);

    LOG_INFO("HS polling mode: %d\n", epld->polling_mode_hs);

    elan_sensor_restart_work();

    return count;
}

static ssize_t elan_sensor_store_hs_center_intt(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct elan_epl_data *epld = epl_data;
    int intt_center;
    LOG_FUN();

    sscanf(buf, "%d",&intt_center);
    HS_INTT_CENTER = intt_center;
    epld->hs_intt = HS_INTT_CENTER;
    LOG_INFO("HS_INTT_CENTER=%d\n", HS_INTT_CENTER);
    LOG_INFO("epld->hs_intt=%d\n", epld->hs_intt);


    elan_sensor_restart_work();

    return count;
}

static ssize_t elan_sensor_show_hs_center_intt(struct device *dev, struct device_attribute *attr, char *buf)
{
    ssize_t len = 0;
    struct elan_epl_data *epld = epl_data;

    LOG_FUN();

    len += snprintf(buf+len, PAGE_SIZE-len, "HS_INTT_CENTER=%d \r\n",HS_INTT_CENTER);
    len += snprintf(buf+len, PAGE_SIZE-len, "epld->hs_intt=%d \r\n",epld->hs_intt);

    return len;
}
#endif

static ssize_t epl2182_show_ps_cal_raw(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct elan_epl_data *obj = epl_data;
    u16 ch1;
    u32 ch1_all=0;
    int count =5;
    int i;
    ssize_t len = 0;

    LOG_FUN();
    if(!epl_data)
    {
        LOG_ERR("epl_data is null!!\n");
        return 0;
    }

    obj->enable_pflag = 1;
    elan_sensor_restart_work();

    for(i=0; i<count; i++)
    {
        //elan_epl2182_psensor_enable(obj, 1);
        msleep(PS_DELAY);
        if(PS_INTERRUPT_MODE == 1)
        {
            elan_sensor_I2C_Write(obj->client,REG_16,R_TWO_BYTE,0x01,0x00);
            elan_sensor_I2C_Read(obj->client);
            gRawData.ps_ch1_raw = (gRawData.raw_bytes[1]<<8) | gRawData.raw_bytes[0];
        }
        LOG_INFO("[%s]: gRawData.ps_ch1_raw=%d \r\n", __FUNCTION__, gRawData.ps_ch1_raw);
		ch1_all = ch1_all+ gRawData.ps_ch1_raw;

    }

    ch1 = (u16)ch1_all/count;
	LOG_INFO("[%s]: ch1 =  %d\n", __FUNCTION__, ch1);

    len += snprintf(buf+len, PAGE_SIZE-len, "%d \r\n", ch1);

	return len;
}

static ssize_t epl2182_show_ps_threshold(struct device *dev, struct device_attribute *attr, char *buf)
{
    ssize_t len = 0;
    struct elan_epl_data *obj = epl_data;

    len += snprintf(buf+len, PAGE_SIZE-len, "gRawData.ps_als_factory(H/L): %d/%d \r\n", gRawData.ps_als_factory.ps_cal_h, gRawData.ps_als_factory.ps_cal_l);
    len += snprintf(buf+len, PAGE_SIZE-len, "ps_threshold(H/L): %d/%d \r\n", obj->ps_threshold_high, obj->ps_threshold_low);
    return len;
}
static ssize_t epl2182_store_ps_threshold(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct elan_epl_data *obj = epl_data;
    int thrl,thrh;

    sscanf(buf, "%d,%d", &thrl , &thrh);
    LOG_INFO("%d,%d\r\n", thrl, thrh);

    obj->ps_threshold_low = thrl;
    obj->ps_threshold_high = thrh;
    gRawData.ps_als_factory.ps_cal_l = thrl;
    gRawData.ps_als_factory.ps_cal_h = thrh;

    elan_sensor_restart_work();

    return count;
}

static DEVICE_ATTR(elan_ls_operationmode, S_IROTH|S_IWOTH, elan_ls_operationmode_show,elan_ls_operationmode_store);
static DEVICE_ATTR(elan_status, S_IROTH|S_IWOTH, elan_ls_status_show,NULL);
#if HS_ENABLE
static DEVICE_ATTR(elan_renvo,    				S_IROTH  | S_IWOTH, elan_sensor_show_renvo,   				NULL);
static DEVICE_ATTR(hs_enable,					S_IROTH  | S_IWOTH, NULL, elan_sensor_store_hs_enable);
static DEVICE_ATTR(hs_raws,					    S_IROTH  | S_IWOTH, elan_sensor_show_hs_raws, NULL);
static DEVICE_ATTR(hs_polling,					S_IROTH  | S_IWOTH, NULL, elan_sensor_store_hs_polling);
static DEVICE_ATTR(hs_cen_intt,					S_IROTH  | S_IWOTH, elan_sensor_show_hs_center_intt, elan_sensor_store_hs_center_intt);
#endif
static DEVICE_ATTR(ps_cal_raw, 				    S_IROTH  | S_IWOTH, epl2182_show_ps_cal_raw, 	 NULL										);
static DEVICE_ATTR(ps_threshold,     			S_IROTH  | S_IWOTH, epl2182_show_ps_threshold,   epl2182_store_ps_threshold					);

static struct attribute *ets_attributes[] =
{
    &dev_attr_elan_ls_operationmode.attr,
    &dev_attr_elan_status.attr,
#if HS_ENABLE
    &dev_attr_elan_renvo.attr,
    &dev_attr_hs_enable.attr,
    &dev_attr_hs_raws.attr,
    &dev_attr_hs_polling.attr,
    &dev_attr_hs_cen_intt.attr,
#endif
    &dev_attr_ps_cal_raw.attr,
    &dev_attr_ps_threshold.attr,
    NULL,
};

static struct attribute_group ets_attr_group =
{
    .attrs = ets_attributes,
};

#if !SPREAD /*SPREAD start ..............*/

static int elan_als_open(struct inode *inode, struct file *file)
{
    struct elan_epl_data *epld = epl_data;

    LOG_FUN();

    if (epld->als_opened)
    {
        return -EBUSY;
    }
    epld->als_opened = 1;

    return 0;
}

static int elan_als_read(struct file *file, char __user *buffer, size_t count, loff_t *ppos)
{
    struct elan_epl_data *epld = epl_data;
    int buf[1];
    if(epld->read_flag ==1)
    {
        buf[0] = gRawData.als_ch1_raw;
        if(copy_to_user(buffer, &buf , sizeof(buf)))
            return 0;
        epld->read_flag = 0;
        return 12;
    }
    else
    {
        return 0;
    }
}

static int elan_als_release(struct inode *inode, struct file *file)
{
    struct elan_epl_data *epld = epl_data;

    LOG_FUN();

    epld->als_opened = 0;

    return 0;
}

static long elan_als_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int flag;
    unsigned long buf[1];
    struct elan_epl_data *epld = epl_data;

    void __user *argp = (void __user *)arg;

    LOG_INFO("als io ctrl cmd %d\n", _IOC_NR(cmd));

    switch(cmd)
    {
        case ELAN_EPL6800_IOCTL_GET_LFLAG:

            LOG_INFO("elan ambient-light IOCTL Sensor get lflag \n");
            flag = epld->enable_lflag;
            if (copy_to_user(argp, &flag, sizeof(flag)))
                return -EFAULT;

            LOG_INFO("elan ambient-light Sensor get lflag %d\n",flag);
            break;

        case ELAN_EPL6800_IOCTL_ENABLE_LFLAG:

            LOG_INFO("elan ambient-light IOCTL Sensor set lflag \n");
            if (copy_from_user(&flag, argp, 1))
                return -EFAULT;
			LOG_INFO("elan ambient-light Sensor set lflag %d\n",flag);
			if(flag) {
				flag = 1;
			}else {
				flag = 0;
			}

            //if (flag < 0 || flag > 1)
            //    return -EINVAL;

            epld->enable_lflag = flag;
            elan_sensor_restart_work();

            //LOG_INFO("elan ambient-light Sensor set lflag %d\n",flag);
            break;

        case ELAN_EPL6800_IOCTL_GETDATA:
            buf[0] = (unsigned long)gRawData.als_ch1_raw;
            if(copy_to_user(argp, &buf , 2))
                return -EFAULT;

            break;

        default:
            LOG_ERR("invalid cmd %d\n", _IOC_NR(cmd));
            return -EINVAL;
    }

    return 0;

}

static unsigned int elan_als_poll(struct file *fp, poll_table * wait)
{
	if(ls_data_changed){
		//pr_info("%s, ls_data_changed 1st, ls_data = 0x%x\n", __func__, ls_data);
		ls_data_changed = 0;
		//mutex_unlock(&stk3x1x_mutex);
		return POLLIN | POLLRDNORM;
	}
	poll_wait(fp, &ls_waitqueue, wait);
	//mutex_unlock(&stk3x1x_mutex);
	return 0;
}

static struct file_operations elan_als_fops =
{
    .owner = THIS_MODULE,
    .open = elan_als_open,
    .read = elan_als_read,
    .release = elan_als_release,
    .unlocked_ioctl = elan_als_ioctl,
    .poll = elan_als_poll,
};

static struct miscdevice elan_als_device =
{
    .minor = MISC_DYNAMIC_MINOR,
    .name = "elan_als",
    .fops = &elan_als_fops
};
#endif /*SPREAD end ..............*/

static int elan_ps_open(struct inode *inode, struct file *file)
{
    struct elan_epl_data *epld = epl_data;

    LOG_FUN();

    if (epld->ps_opened)
        return -EBUSY;

    epld->ps_opened = 1;

    return 0;
}

static int elan_ps_release(struct inode *inode, struct file *file)
{
    struct elan_epl_data *epld = epl_data;

    LOG_FUN();

    epld->ps_opened = 0;

    psensor_mode_suspend = 0;

    return 0;
}

static long elan_ps_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    unsigned int value;
    int flag;
    struct elan_epl_data *epld = epl_data;
    void __user *argp = (void __user *)arg;

    LOG_INFO("ps io ctrl cmd %d\n", _IOC_NR(cmd));

    //ioctl message handle must define by android sensor library (case by case)
    switch(cmd)
    {
#if 0
        case ELAN_EPL6800_IOCTL_GET_PFLAG:

            LOG_INFO("elan Proximity Sensor IOCTL get pflag \n");
            flag = epld->enable_pflag;
            if (copy_to_user(argp, &flag, sizeof(flag)))
                return -EFAULT;

            LOG_INFO("elan Proximity Sensor get pflag %d\n",flag);
            break;

        case ELAN_EPL6800_IOCTL_ENABLE_PFLAG:
            LOG_INFO("elan Proximity IOCTL Sensor set pflag \n");
            if (copy_from_user(&flag, argp, sizeof(flag)))
                return -EFAULT;
			if(flag) {
			    wake_lock(&ps_lock);
#if DYN_ENABLE
		        gRawData.ps_min_raw=0xffff;
#endif
				flag = 1;
			}else {
				flag = 0;
				wake_unlock(&ps_lock);
			}
            //if (flag < 0 || flag > 1)
            //    return -EINVAL;

            epld->enable_pflag = flag;
            elan_sensor_restart_work();

            LOG_INFO("elan Proximity Sensor set pflag %d\n",flag);
            break;
#if SPREAD
        case ELAN_EPL6800_IOCTL_GET_LFLAG:

            LOG_INFO("elan ambient-light IOCTL Sensor get lflag \n");
            flag = epld->enable_lflag;
            if (copy_to_user(argp, &flag, sizeof(flag)))
                return -EFAULT;

            LOG_INFO("elan ambient-light Sensor get lflag %d\n",flag);
            break;

        case ELAN_EPL6800_IOCTL_ENABLE_LFLAG:

            LOG_INFO("elan ambient-light IOCTL Sensor set lflag \n");
            if (copy_from_user(&flag, argp, 1))
                return -EFAULT;
			LOG_INFO("elan ambient-light Sensor set lflag %d\n",flag);
			if(flag) {
				flag = 1;
			}else {
				flag = 0;
			}
            //if (flag < 0 || flag > 1)
            //    return -EINVAL;

            epld->enable_lflag = flag;
            elan_sensor_restart_work();

            //LOG_INFO("elan ambient-light Sensor set lflag %d\n",flag);
            break;

#endif
        case ELAN_EPL6800_IOCTL_GETDATA:

            //value = gRawData.ps_ch1_raw;
            value = gRawData.ps_state;
            if(copy_to_user(argp, &value , 1))
                return -EFAULT;

            LOG_INFO("elan proximity Sensor get data (%d) \n",value);
            break;
#endif
        case ALSPS_GET_PS_RAW_DATA:
            //value = gRawData.ps_ch1_raw;
            value = gRawData.ps_ch1_raw;
            if(copy_to_user(argp, &value , 2))
                return -EFAULT;

            LOG_INFO("elan proximity Sensor get data (%d) \n",value);
            break;
			
	   case ALSPS_GET_PS_RAW_DATA_L:

            value = epld->ps_threshold_low;
            if(copy_to_user(argp, &value , 2))
                return -EFAULT;

            LOG_INFO("elan proximity Sensor get data (%d) \n",value);
            break;
			
	   case ALSPS_GET_PS_RAW_DATA_H:

            value = epld->ps_threshold_high;
            if(copy_to_user(argp, &value , 2))
                return -EFAULT;

            LOG_INFO("elan proximity Sensor get data (%d) \n",value);
            break;
        default:
            LOG_ERR("invalid cmd %d\n", _IOC_NR(cmd));
            return -EINVAL;
    }

    return 0;
}

static unsigned int elan_ps_poll(struct file *fp, poll_table * wait)
{
	if(ps_data_changed) {
		ps_data_changed = 0;
		return POLLIN | POLLRDNORM;
	}
	poll_wait(fp, &ps_waitqueue, wait);
	return 0;
}


static struct file_operations elan_ps_fops =
{
    .owner = THIS_MODULE,
    .open = elan_ps_open,
    .release = elan_ps_release,
    .unlocked_ioctl = elan_ps_ioctl,
    .poll		= elan_ps_poll,
};

static struct miscdevice elan_ps_device =
{
    .minor = MISC_DYNAMIC_MINOR,
#ifdef CONFIG_ZYT_SUPPORT
	.name = "zyt_pls_cdc",
#else
#ifdef EPL_SINGLE_INPUT_DEVICE
	.name = "elan_pls",
#else
    .name = "elan_ps",
#endif
#endif
    .fops = &elan_ps_fops
};

static int initial_sensor(struct elan_epl_data *epld)
{
    struct i2c_client *client = epld->client;

    int ret = 0;

    LOG_INFO("initial_sensor enter!\n");

    ret = elan_sensor_I2C_Read(client);

    if(ret < 0)
        return -EINVAL;

    elan_sensor_I2C_Write(client,REG_0,W_SINGLE_BYTE,0x02, EPL_S_SENSING_MODE);
    elan_sensor_I2C_Write(client,REG_9,W_SINGLE_BYTE,0x02,EPL_INT_DISABLE);
    //set_psensor_intr_threshold(P_SENSOR_LTHD , P_SENSOR_HTHD);
#if HS_ENABLE
    elan_sensor_I2C_Write(client,REG_19,R_TWO_BYTE,0x01,0x00);
    elan_sensor_I2C_Read(client);
    gRawData.renvo = (gRawData.raw_bytes[1]<<8)|gRawData.raw_bytes[0];
#endif

    msleep(2);

    epld->enable_lflag = 0;
    epld->enable_pflag = 0;

    return ret;
}

/*----------------------------------------------------------------------------*/
static ssize_t light_enable_show(struct device *dev,
					 struct device_attribute *attr, char *buf)
{
	struct elan_epl_data *epld  = epl_data;
	printk("%s: ALS_status=%d\n", __func__, epld->enable_lflag);
	return sprintf(buf, "%d\n", epld->enable_lflag);
}

static ssize_t light_enable_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t size)
{
	struct elan_epl_data *epld = epl_data;

    LOG_INFO("light_enable_store: enable=%s \n", buf);

	if (sysfs_streq(buf, "1")){
		epld->enable_lflag= 1;
    }
	else if (sysfs_streq(buf, "0"))
		epld->enable_lflag= 0;
	else {
		LOG_ERR("%s: invalid value %d\n", __func__, *buf);
		return 0;
	}

	elan_sensor_restart_work();
	return size;
}

/*----------------------------------------------------------------------------*/
static struct device_attribute dev_attr_light_enable =
__ATTR(enable, S_IRWXUGO,
	   light_enable_show, light_enable_store);

static struct attribute *light_sysfs_attrs[] = {
	&dev_attr_light_enable.attr,
	NULL
};

static struct attribute_group light_attribute_group = {
	.attrs = light_sysfs_attrs,
};
/*----------------------------------------------------------------------------*/

static int lightsensor_setup(struct elan_epl_data *epld)
{
    int err = 0;
    LOG_INFO("lightsensor_setup enter.\n");

#ifdef EPL_SINGLE_INPUT_DEVICE
	epld->als_input_dev = epld->ps_input_dev;
#else
    epld->als_input_dev = input_allocate_device();
    if (!epld->als_input_dev)
    {
        LOG_ERR( "could not allocate ls input device\n");
        return -ENOMEM;
    }
#endif
#ifdef EPL_SINGLE_INPUT_DEVICE
	epld->als_input_dev->name = ElanPS_Name;
#else
    epld->als_input_dev->name = ElanALsensorName;
#endif
    set_bit(EV_ABS, epld->als_input_dev->evbit);
    input_set_abs_params(epld->als_input_dev, ABS_MISC, 0, 9, 0, 0);

#ifndef EPL_SINGLE_INPUT_DEVICE
    err = input_register_device(epld->als_input_dev);
    if (err < 0)
    {
        LOG_ERR("can not register ls input device\n");
        goto err_free_ls_input_device;
    }

    err = misc_register(&elan_als_device);
    if (err < 0)
    {
        LOG_ERR("can not register ls misc device\n");
        goto err_unregister_ls_input_device;
    }
#endif

    err = sysfs_create_group(&epld->als_input_dev->dev.kobj, &light_attribute_group);

	if (err) {
		pr_err("%s: could not create sysfs group\n", __func__);
		goto err_free_ls_input_device;
	}

    return err;

err_unregister_ls_input_device:
#ifndef EPL_SINGLE_INPUT_DEVICE
    input_unregister_device(epld->als_input_dev);
#endif
err_free_ls_input_device:
#ifndef EPL_SINGLE_INPUT_DEVICE
    input_free_device(epld->als_input_dev);
#endif
    return err;
}


/*----------------------------------------------------------------------------*/
static ssize_t proximity_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct elan_epl_data *epld = epl_data;
	printk("%s: PS status=%d\n", __func__, epld->enable_pflag);
	LOG_INFO("epl2182_setup_psensor enter.\n");
	return sprintf(buf, "%d\n", epld->enable_pflag);
}
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
static ssize_t proximity_enable_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
    struct elan_epl_data *epld = epl_data;

    LOG_INFO("proximity_enable_store: enable=%s \n", buf);
	if (sysfs_streq(buf, "1"))
	{
		wake_lock(&ps_lock);
#if DYN_ENABLE
		gRawData.ps_min_raw=0xffff;
#endif
		epld->enable_pflag =1;
    }
	else if (sysfs_streq(buf, "0"))
	{
	    epld->enable_pflag =0;
        wake_unlock(&ps_lock);
    }
	else
	{
		LOG_ERR("%s: invalid value %d\n", __func__, *buf);
		return 0;
	}

	elan_sensor_restart_work();
	return size;
}
/*----------------------------------------------------------------------------*/

#ifdef CONFIG_ZYT_PLSENSOR_COMPATIBLE
static ssize_t chip_id_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", 0x2182);
}


static struct device_attribute cdc_als_enable_attribute = __ATTR(als_enable,0666,light_enable_show,light_enable_store);
//static struct device_attribute cdc_als_poll_delay_attribute = __ATTR(als_delay,0666,cdc_als_delay_show,cdc_als_delay_store);

static struct device_attribute cdc_ps_enable_attribute = __ATTR(ps_enable,0666,proximity_enable_show,proximity_enable_store);

static struct device_attribute cdc_chip_id_attribute = __ATTR(chip_id,0666,chip_id_show,NULL);

static struct attribute* sensors_pls_cdc_attrs [] =
{
	&cdc_als_enable_attribute.attr,
	//&cdc_als_poll_delay_attribute.attr,
	&cdc_ps_enable_attribute.attr,
	&cdc_chip_id_attribute.attr,
	NULL,
};

static const struct attribute_group pls_cdc_attrs_group = {
	.attrs = sensors_pls_cdc_attrs,
};


static int check_plsensor_chip(void)
{
	CDC_Plsensor_Device_Id(0x2182);

	return 0;
}

static int remove_plsensor_chip(void)
{
	CDC_Plsensor_Device_Id(0xFFFF);

	return 0;
}

#else


static struct device_attribute dev_attr_ps_enable =
__ATTR(enable, S_IRWXUGO,
	   proximity_enable_show, proximity_enable_store);

static struct attribute *proximity_sysfs_attrs[] = {
	&dev_attr_ps_enable.attr,
	NULL
};

static struct attribute_group proximity_attribute_group = {
	.attrs = proximity_sysfs_attrs,
};
/*----------------------------------------------------------------------------*/

#endif

static int psensor_setup(struct elan_epl_data *epld)
{
    int err = 0;
    LOG_INFO("psensor_setup enter.\n");

    epld->ps_input_dev = input_allocate_device();
    if (!epld->ps_input_dev)
    {
        LOG_ERR("could not allocate ps input device\n");
        return -ENOMEM;
    }
	
#ifdef EPL_SINGLE_INPUT_DEVICE
	epld->ps_input_dev->name = ElanPS_Name;
#else
    epld->ps_input_dev->name = ElanPsensorName;
#endif

    set_bit(EV_ABS, epld->ps_input_dev->evbit);
    input_set_abs_params(epld->ps_input_dev, ABS_DISTANCE, 0, 1, 0, 0);

    err = input_register_device(epld->ps_input_dev);
    if (err < 0)
    {
        LOG_ERR("could not register ps input device\n");
        goto err_free_ps_input_device;
    }

    err = misc_register(&elan_ps_device);
    if (err < 0)
    {
        LOG_ERR("could not register ps misc device\n");
        goto err_unregister_ps_input_device;
    }
#ifdef CONFIG_ZYT_PLSENSOR_COMPATIBLE
    err = sysfs_create_group(&epld->ps_input_dev->dev.kobj, &pls_cdc_attrs_group);
	if (err) {
		pr_err("%s: PS could not create sysfs group\n", __func__);
		goto err_free_ps_input_device;
	}
#else
    err = sysfs_create_group(&epld->ps_input_dev->dev.kobj, &proximity_attribute_group);
	if (err) {
		pr_err("%s: PS could not create sysfs group\n", __func__);
		goto err_free_ps_input_device;
	}

#endif

    return err;

err_unregister_ps_input_device:
    input_unregister_device(epld->ps_input_dev);
err_free_ps_input_device:
    input_free_device(epld->ps_input_dev);
    return err;
}

#if PS_INTERRUPT_MODE || HS_ENABLE
static int setup_interrupt(struct elan_epl_data *epld)
{
    struct i2c_client *client = epld->client;
#if SPREAD
    struct elan_epl_platform_data *pdata = client->dev.platform_data;
#endif
    int err = 0;
    msleep(5);
#if S5PV210
    err = gpio_request(S5PV210_GPH0(1), "Elan EPL IRQ");
    if (err)
    {
        LOG_ERR("gpio pin request fail (%d)\n", err);
        goto initial_fail;
    }
    else
    {
        LOG_INFO("----- Samsung gpio config success -----\n");
        s3c_gpio_cfgpin(S5PV210_GPH0(1),S3C_GPIO_SFN(0x0F)/*(S5PV210_GPH0_1_EXT_INT30_1) */);
        s3c_gpio_setpull(S5PV210_GPH0(1), S3C_GPIO_PULL_UP); //S3C_GPIO_PULL_NONE);
    }

#elif SPREAD

    err = gpio_request(pdata->intr_gpio, "Elan EPL IRQ");
    if (err)
    {
        LOG_ERR("gpio pin request fail (%d)\n", err);
        goto initial_fail;
    }
    else
    {

		gpio_direction_input(pdata->intr_gpio);

		/*get irq*/
		client->irq = gpio_to_irq(pdata->intr_gpio);
		epld->irq = client->irq;

		printk("IRQ number is %d\n", client->irq);

    }
#endif
    err = request_irq(epld->irq,elan_sensor_irq_handler, IRQF_TRIGGER_FALLING ,
                      client->dev.driver->name, epld);
    if(err <0)
    {
        LOG_ERR("request irq pin %d fail for gpio\n",err);
        goto fail_free_intr_pin;
    }

    return err;

initial_fail:
fail_free_intr_pin:
    gpio_free(epld->intr_pin);
    return err;
}
#endif

#ifdef CONFIG_SUSPEND
static int elan_sensor_suspend(struct i2c_client *client, pm_message_t mesg)
{
    LOG_FUN();
#if 0
    if(epl_data->enable_pflag==0 )
    {
        elan_sensor_I2C_Write(client,REG_7, W_SINGLE_BYTE, 0x02, EPL_C_P_DOWN);
        cancel_delayed_work(&polling_work);
    }
#endif
    return 0;
}

static void elan_sensor_early_suspend(struct early_suspend *h)
{
    struct elan_epl_data *epld = epl_data;
    //struct i2c_client *client = epld->client;
    LOG_FUN();

    epld->enable_lflag = 1;
    if( epld->enable_pflag==0)
    {
        LOG_INFO("[%s]: enable_pflag=%d \r\n", __func__, epld->enable_pflag);
        elan_sensor_lsensor_enable(epld);
    }

    psensor_mode_suspend = 1;
}

static int elan_sensor_resume(struct i2c_client *client)
{
    //struct elan_epl_data *epld = epl_data;
    LOG_FUN();
#if 0
    if(epld->enable_pflag | epld->enable_lflag)
    {
        elan_sensor_I2C_Write(client,REG_7, W_SINGLE_BYTE, 0x02, EPL_C_P_UP);
    }

    if(epld->enable_pflag)
    {
        elan_sensor_restart_work();
    }
#endif
    return 0;
}

static void elan_sensor_late_resume(struct early_suspend *h)
{
    struct elan_epl_data *epld = epl_data;
    //struct i2c_client *client = epld->client;

    LOG_FUN();

    //epld->als_suspend = 0;
    //epld->ps_suspend = 0;
    //epld->hs_suspend =  0;
#if 0
    if(obj->hw->polling_mode_ps == 0)
		gRawData.ps_suspend_flag = false;
#endif
    if(epld->enable_lflag==1)
        elan_sensor_restart_work();

    psensor_mode_suspend = 0;
}
#endif

static int elan_sensor_probe(struct i2c_client *client,const struct i2c_device_id *id)
{
    int err = 0;
    struct elan_epl_data *epld ;
    //struct elan_epl_platform_data *pdata;
    static struct platform_device *sensor_dev;
#ifdef CONFIG_OF
    struct elan_epl_platform_data  *pdata = client->dev.platform_data;
    struct device_node *np = client->dev.of_node;
#endif
    LOG_INFO("elan sensor probe enter.\n");

#ifdef CONFIG_OF
        if (np && !pdata){
    		pdata = kzalloc(sizeof(*pdata), GFP_KERNEL);
    		if (!pdata) {
    			dev_err(&client->dev, "Could not allocate struct ltr558_pls_platform_data");
			goto exit_allocate_pdata_failed;
		}
		pdata->intr_gpio= of_get_gpio(np, 0);
		if(pdata->intr_gpio < 0){
			dev_err(&client->dev, "fail to get irq_gpio_number\n");
			kfree(pdata);
			goto exit_irq_gpio_read_fail;
		}
		printk("pdata->intr_gpio=%d\n",pdata->intr_gpio);
		client->dev.platform_data = pdata;
	}
#endif

#ifdef CONFIG_ZYT_PLSENSOR_COMPATIBLE
	if(CDC_Plsensor_Device_Id(0)!=0)
	{
		printk("PlSensor(0x%x)Exist!", CDC_Plsensor_Device_Id(0));
		err = -ENODEV;
		goto exit_check_functionality_failed;
	}
#endif
    if((i2c_smbus_read_byte_data(client, 0x98)) != 0x68)
    {
        LOG_INFO("elan ALS/PS sensor is failed. \n");
	err = -ENODEV;
        goto i2c_fail;
    }
	
/*
    if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE))
    {
       // dev_err(&client->dev,"No supported i2c func what we need?!!\n");
        err = -EIO;
        goto i2c_fail;
    }
*/
    epld = kzalloc(sizeof(struct elan_epl_data), GFP_KERNEL);
    if (!epld)
        return -ENOMEM;
    /*LOG_INFO("chip id REG 0x00 value = %8x\n", i2c_smbus_read_byte_data(client, 0x00));
    LOG_INFO("chip id REG 0x01 value = %8x\n", i2c_smbus_read_byte_data(client, 0x08));
    LOG_INFO("chip id REG 0x02 value = %8x\n", i2c_smbus_read_byte_data(client, 0x10));
    LOG_INFO("chip id REG 0x03 value = %8x\n", i2c_smbus_read_byte_data(client, 0x18));
    LOG_INFO("chip id REG 0x04 value = %8x\n", i2c_smbus_read_byte_data(client, 0x20));
    LOG_INFO("chip id REG 0x05 value = %8x\n", i2c_smbus_read_byte_data(client, 0x28));
    LOG_INFO("chip id REG 0x06 value = %8x\n", i2c_smbus_read_byte_data(client, 0x30));
    LOG_INFO("chip id REG 0x07 value = %8x\n", i2c_smbus_read_byte_data(client, 0x38));
    LOG_INFO("chip id REG 0x09 value = %8x\n", i2c_smbus_read_byte_data(client, 0x48));
    LOG_INFO("chip id REG 0x0D value = %8x\n", i2c_smbus_read_byte_data(client, 0x68));
    LOG_INFO("chip id REG 0x0E value = %8x\n", i2c_smbus_read_byte_data(client, 0x70));
    LOG_INFO("chip id REG 0x0F value = %8x\n", i2c_smbus_read_byte_data(client, 0x71));
    LOG_INFO("chip id REG 0x10 value = %8x\n", i2c_smbus_read_byte_data(client, 0x80));
    LOG_INFO("chip id REG 0x11 value = %8x\n", i2c_smbus_read_byte_data(client, 0x88));
    LOG_INFO("chip id REG 0x13 value = %8x\n", i2c_smbus_read_byte_data(client, 0x98));*/

    epld->client = client;
	//add jinq
	if(client->dev.platform_data != NULL)		
		{
			 pdata=client->dev.platform_data;	
			epld->irq =pdata->intr_gpio;
		    LOG_INFO(" epld->irq===========%d\n", epld->irq);
		}
   // epld->irq = client->irq;
    i2c_set_clientdata(client, epld);

    epl_data = epld;

    epld->ps_threshold_low = P_SENSOR_LTHD;
    epld->ps_threshold_high = P_SENSOR_HTHD;
    gRawData.ps_als_factory.cal_file_exist = 1;
    gRawData.ps_als_factory.cal_finished = 0;
    epld->epl_wq = create_singlethread_workqueue("elan_sensor_wq");
    if (!epld->epl_wq)
    {
        LOG_ERR("can't create workqueue\n");
        err = -ENOMEM;
        goto err_create_singlethread_workqueue;
    }
#if HS_ENABLE
    mutex_init(&sensor_mutex);
    epld->polling_mode_hs = HS_POLLING_MODE;
    epld->hs_intt = HS_INTT_CENTER;
#endif

    err = psensor_setup(epld);
    if (err < 0)
    {
        LOG_ERR("psensor_setup error!!\n");
        goto err_psensor_setup;
    }

	err = lightsensor_setup(epld);
    if (err < 0)
    {
        LOG_ERR("lightsensor_setup error!!\n");
        goto err_lightsensor_setup;
    }
	
    err = initial_sensor(epld);
    if (err < 0)
    {
        LOG_ERR("fail to initial sensor (%d)\n", err);
        goto err_sensor_setup;
    }

#ifdef CONFIG_ZYT_PLSENSOR_COMPATIBLE
	if(check_plsensor_chip()<0)
	{
		err = -ENODEV;
		printk("Fail to find epl2128");
		goto exit_check_chip_failed;
	}
#endif


#if PS_INTERRUPT_MODE || HS_ENABLE
    err = setup_interrupt(epld);
    if (err < 0)
    {
        LOG_ERR("setup error!\n");
        goto err_sensor_setup;
    }
#endif

#ifdef CONFIG_SUSPEND
    epld->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
    epld->early_suspend.suspend = elan_sensor_early_suspend;
    epld->early_suspend.resume = elan_sensor_late_resume;
    register_early_suspend(&epld->early_suspend);
#endif
    wake_lock_init(&ps_lock, WAKE_LOCK_SUSPEND, "ps_wakelock");

    sensor_dev = platform_device_register_simple("elan_alsps", -1, NULL, 0);
    if (IS_ERR(sensor_dev))
    {
        printk ("sensor_dev_init: error\n");
        goto err_fail;
    }

    err = sysfs_create_group(&sensor_dev->dev.kobj, &ets_attr_group);
    if (err !=0)
    {
        dev_err(&client->dev,"%s:create sysfs group error", __func__);
        goto err_fail;
    }

    LOG_INFO("sensor probe success.\n");
	{
		extern void zyt_info_s2(char* ,char*);
		zyt_info_s2("[PLSensor] : ","epl2182_cdc");
	}
    return err;

err_fail:
#ifndef EPL_SINGLE_INPUT_DEVICE
    input_unregister_device(epld->als_input_dev);
    input_unregister_device(epld->ps_input_dev);
    input_free_device(epld->als_input_dev);
    input_free_device(epld->ps_input_dev);
#else
    input_unregister_device(epld->ps_input_dev);
    input_free_device(epld->ps_input_dev);
#endif
exit_check_chip_failed:
#ifdef CONFIG_ZYT_PLSENSOR_COMPATIBLE
	remove_plsensor_chip();
#endif
err_lightsensor_setup:
err_psensor_setup:
err_sensor_setup:
    destroy_workqueue(epld->epl_wq);
    misc_deregister(&elan_ps_device);
#ifndef EPL_SINGLE_INPUT_DEVICE
    misc_deregister(&elan_als_device);
#endif
err_create_singlethread_workqueue:
kfree(epld);
i2c_fail:
exit_check_functionality_failed:
//err_platform_data_null:
//    kfree(epld);
exit_irq_gpio_read_fail:
exit_allocate_pdata_failed:
    return err;
}

static int elan_sensor_remove(struct i2c_client *client)
{
    struct elan_epl_data *epld = i2c_get_clientdata(client);

    dev_dbg(&client->dev, "%s: enter.\n", __func__);

 //   unregister_early_suspend(&epld->early_suspend);   zhujp modify
#ifndef EPL_SINGLE_INPUT_DEVICE
    input_unregister_device(epld->als_input_dev);
    input_unregister_device(epld->ps_input_dev);
    input_free_device(epld->als_input_dev);
    input_free_device(epld->ps_input_dev);
	misc_deregister(&elan_ps_device);
    misc_deregister(&elan_als_device);
#else
    input_unregister_device(epld->ps_input_dev);
    input_free_device(epld->ps_input_dev);
	misc_deregister(&elan_ps_device);
#endif
    free_irq(epld->irq,epld);
    destroy_workqueue(epld->epl_wq);
    kfree(epld);
    return 0;
}

static const struct i2c_device_id elan_sensor_id[] =
{
    { "epl2182_pls", 0 },
    { }
};

#if defined(ZYT_CDC_EPL_COMPATIBLE_WITH_259X_DTS)
static struct of_device_id epl_match_table[] = {
                { .compatible = "epl,epl259x",},
                { },
              };
#else
static struct of_device_id epl_match_table[] = {
                { .compatible = "ELAN,epl2182_pls",},
                { },
        };
#endif

static struct i2c_driver elan_sensor_driver =
{
    .probe	= elan_sensor_probe,
    .remove	= elan_sensor_remove,
    .id_table	= elan_sensor_id,
    .driver	= {
        .name = "epl2182_pls",
        .owner = THIS_MODULE,
        .of_match_table =epl_match_table,
    },
#ifdef CONFIG_SUSPEND
    .suspend = elan_sensor_suspend,
    .resume = elan_sensor_resume,
#endif
};

static int __init elan_sensor_init(void)
{
#ifdef CONFIG_ZYT_PLSENSOR_COMPATIBLE
	if(CDC_Plsensor_Device_Id(0)!=0)
	{
		printk("PlSensor(0x%x)Exist!", CDC_Plsensor_Device_Id(0));
		return -ENODEV;
	}
#endif
    return i2c_add_driver(&elan_sensor_driver);
}

static void __exit  elan_sensor_exit(void)
{
    i2c_del_driver(&elan_sensor_driver);
}

module_init(elan_sensor_init);
module_exit(elan_sensor_exit);

MODULE_AUTHOR("Renato Pan <renato.pan@eminent-tek.com>");
MODULE_DESCRIPTION("ELAN epl2182 driver");
MODULE_LICENSE("GPL");
