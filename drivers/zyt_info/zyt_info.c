/*-----------------------------------------------------------------------------*/
// File Name : zyt_info.c
// for kernel debug info
// Author : wangming
// Date : 2015-1-18
/*-----------------------------------------------------------------------------*/
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/device.h>
#include <asm/uaccess.h>

#include <linux/stat.h>
#include <soc/sprd/board.h>

#define BUFFER_LEN 1024
static char zyt_info_buffer[BUFFER_LEN]={0};

#if defined(ZCFG_SC2703_READ_OTP)&&defined(CONFIG_BACKLIGHT_SC2703)
static uint32_t sc2703_val_1=0;
static int __init sc2703_start(char *str)
{ 
	printk("%s: line= %d\n", __func__, __LINE__); 
	if ((str != NULL) && (str[0] == '0') && (str[1] == 'x')) {
		sscanf(&str[2], "%x", &sc2703_val_1);
	}
	printk("sc2703_val_1 = 0x%x\n", sc2703_val_1);

	return 1;
}

__setup("sc2703=", sc2703_start);
#endif

void zyt_info(char* content)
{
	unsigned int content_len = strlen(content);
	unsigned int buffer_used = strlen(zyt_info_buffer);
	if(content == NULL) return;
	if((buffer_used + content_len) > BUFFER_LEN) return;
	strcat(zyt_info_buffer,content);
}

void zyt_info_s(char* c1){zyt_info(c1);zyt_info("\n");}
void zyt_info_s2(char* c1,char* c2){zyt_info(c1);zyt_info(c2);zyt_info("\n");}
void zyt_info_sx(char* c1,int x){char temp[50];sprintf(temp,"%s 0x%x",c1,x);zyt_info(temp);zyt_info("\n");}

EXPORT_SYMBOL_GPL(zyt_info_s);
EXPORT_SYMBOL_GPL(zyt_info_s2);
EXPORT_SYMBOL_GPL(zyt_info_sx);

extern char* zyt_get_lcm_info(void);
#ifdef CONFIG_SPRD_CAMERA
extern char* get_sensor_info(int sensor_id);/*sensor_drv_k.c*/
#else
static char* get_sensor_info(int sensor_id) { return "N/A"; }
#endif
extern char *zyt_battery_info(void);
static int zyt_info_proc_show(struct seq_file *m, void *v) {
#if defined(ZCFG_SC2703_READ_OTP)&&defined(CONFIG_BACKLIGHT_SC2703)
	seq_printf(m, "[LCD] : %s\n[MainSensor] : %s\n[Main2Sensor] : %s\n[SubSensor] : %s\n[Sub2Sensor] : %s\n%s%s\n[SC2703] : 0x%x\n",
					zyt_get_lcm_info(),
					get_sensor_info(0),
					get_sensor_info(2),
					get_sensor_info(1),
					get_sensor_info(3),
					zyt_info_buffer,
					zyt_battery_info(),
					sc2703_val_1
					);
#else
	seq_printf(m, "[LCD] : %s\n[MainSensor] : %s\n[Main2Sensor] : %s\n[SubSensor] : %s\n[Sub2Sensor] : %s\n%s%s\n",
					zyt_get_lcm_info(),
					get_sensor_info(0),
					get_sensor_info(2),
					get_sensor_info(1),
					get_sensor_info(3),
					zyt_info_buffer,
					zyt_battery_info()
					);
#endif
	return 0;
}

static int zyt_info_proc_open(struct inode *inode, struct file *file) {
 return single_open(file, zyt_info_proc_show, NULL);
}

static const struct proc_ops zyt_info_proc_fops = {
 .proc_open = zyt_info_proc_open,
 .proc_read = seq_read,
 .proc_lseek = seq_lseek,
 .proc_release = single_release,
};


int zyt_adb_debug=0;  
module_param(zyt_adb_debug,int,S_IRUGO|S_IWUSR);


static int __init zyt_info_proc_init(void) {
 proc_create("zyt_info", 0, NULL, &zyt_info_proc_fops);
 return 0;
}

static void __exit zyt_info_proc_exit(void) {
 remove_proc_entry("zyt_info", NULL);
}

MODULE_LICENSE("GPL");
module_init(zyt_info_proc_init);
module_exit(zyt_info_proc_exit);
