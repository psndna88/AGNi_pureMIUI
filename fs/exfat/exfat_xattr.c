#include <linux/file.h>
#include <linux/fs.h>
#include <linux/xattr.h>
#include <linux/dcache.h>
#include "exfat.h"

#ifndef CONFIG_EXFAT_VIRTUAL_XATTR_SELINUX_LABEL
#define CONFIG_EXFAT_VIRTUAL_XATTR_SELINUX_LABEL	("undefined")
#endif

static const char default_xattr[] = CONFIG_EXFAT_VIRTUAL_XATTR_SELINUX_LABEL;

int exfat_setxattr(struct dentry *dentry, const char *name, const void *value, size_t size, int flags) {
	if (!name || strcmp(name, "security.selinux"))
		return -EOPNOTSUPP;
	return 0;
}

ssize_t exfat_getxattr(struct dentry *dentry, const char *name, void *value, size_t size) {
	if (!name || strcmp(name, "security.selinux"))
		return -EOPNOTSUPP;
	if (size > strlen(default_xattr)+1 && value)
		strcpy(value, default_xattr);
	return strlen(default_xattr);
}

ssize_t exfat_listxattr(struct dentry *dentry, char *list, size_t size) {
	return 0;
}

int exfat_removexattr(struct dentry *dentry, const char *name) {
	if (!name || strcmp(name, "security.selinux"))
		return -EOPNOTSUPP;
	return 0;
}


