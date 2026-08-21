/*
 * 32-bit compat ADF ioctl layer.
 * DW99 port: 7.2 removed compat_alloc_user_space/copy_in_user,
 * so 32-bit compat ioctls are disabled (-ENOTTY).
 */
#include <linux/uaccess.h>
#include <video/adf.h>

#include "adf_fops.h"
#include "adf_fops32.h"

long adf_compat_post_config(struct file *file,
		struct adf_post_config32 __user *arg)
{
	return -ENOTTY;
}

long adf_compat_get_device_data(struct file *file,
		struct adf_device_data32 __user *arg)
{
	return -ENOTTY;
}

long adf_compat_get_interface_data(struct file *file,
		struct adf_interface_data32 __user *arg)
{
	return -ENOTTY;
}

long adf_compat_get_overlay_engine_data(struct file *file,
		struct adf_overlay_engine_data32 __user *arg)
{
	return -ENOTTY;
}

long adf_file_compat_ioctl(struct file *file, unsigned int cmd,
		unsigned long arg)
{
	return -ENOTTY;
}
