// SPDX-License-Identifier: GPL-2.0-only
/*
 * exFAT virtual xattr support (for SELinux label)
 * Ported from sprd 4.4
 */
#include <linux/xattr.h>
#include <linux/fs.h>
#include <linux/dcache.h>

#ifndef CONFIG_EXFAT_VIRTUAL_XATTR_SELINUX_LABEL
#define CONFIG_EXFAT_VIRTUAL_XATTR_SELINUX_LABEL	("undefined")
#endif

static const char exfat_default_selinux[] = CONFIG_EXFAT_VIRTUAL_XATTR_SELINUX_LABEL;

static int exfat_xattr_get(const struct xattr_handler *handler,
			   struct dentry *dentry, struct inode *inode,
			   const char *name, void *buffer, size_t size)
{
	if (strcmp(name, "selinux"))
		return -EOPNOTSUPP;
	if (size > strlen(exfat_default_selinux) + 1 && buffer)
		strcpy(buffer, exfat_default_selinux);
	return strlen(exfat_default_selinux);
}

static int exfat_xattr_set(const struct xattr_handler *handler,
			   struct mnt_idmap *idmap, struct dentry *dentry,
			   struct inode *inode, const char *name,
			   const void *buffer, size_t size, int flags)
{
	if (strcmp(name, "selinux"))
		return -EOPNOTSUPP;
	return 0;
}

static const struct xattr_handler exfat_xattr_handler = {
	.prefix = XATTR_SECURITY_PREFIX,
	.get = exfat_xattr_get,
	.set = exfat_xattr_set,
};

const struct xattr_handler *exfat_xattr_handlers[] = {
	&exfat_xattr_handler,
	NULL,
};
