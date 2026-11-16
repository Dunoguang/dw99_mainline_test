#ifndef __KSU_H_SELINUX_HIDE
#define __KSU_H_SELINUX_HIDE
#include <linux/types.h>
#include <linux/version.h>
#include <linux/sched.h>

void ksu_selinux_hide_init();
void ksu_selinux_hide_exit();
void ksu_selinux_hide_drop_backup_if_unused();
void ksu_selinux_hide_handle_second_stage();
void ksu_selinux_hide_handle_post_fs_data();

// https://github.com/torvalds/linux/commit/b21507e272627c434e8dd74e8d51fd8245281b59
/*
 * The kernel side of SuSFS (security/selinux/{hooks,selinuxfs}.c) resolves
 * policy contexts through these helpers, so they are exported when the
 * manual selinux_hide hooks are used.
 */
#ifdef KSU_COMPAT_HAS_SUSFS_FEATURE_SELINUX_HIDE
struct selinux_policy;
struct av_decision;
struct file;

int security_context_to_sid_with_policy(struct selinux_policy *policy,
					const char *scontext, u32 scontext_len,
					u32 *sid, u32 def_sid, gfp_t gfp_flags,
					u32 *orig_sid_p, int *orig_rc_p);
int security_sid_to_context_with_policy(struct selinux_policy *policy, u32 sid,
					char **scontext, u32 *scontext_len);
void security_compute_av_user_with_policy(struct selinux_policy *policy,
					  u32 ssid, u32 tsid, u16 tclass,
					  struct av_decision *avd);
ssize_t my_write_context(struct file *file, char *buf, size_t size);
ssize_t my_write_access(struct file *file, char *buf, size_t size);
int my_setprocattr(const char *name, void *value, size_t size);
#endif // #ifdef KSU_COMPAT_HAS_SUSFS_FEATURE_SELINUX_HIDE

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0) || defined(KSU_COMPAT_SETPROCATTR_USE_NEW_PROTOTYPE)
typedef int (*setprocattr_fn)(const char *name, void *value, size_t size);

int ksu_handle_selinux_setprocattr(const char *name, void *value, size_t size);
#else
typedef int (*setprocattr_fn)(struct task_struct *p, char *name, void *value, size_t size);

int ksu_handle_selinux_setprocattr(struct task_struct *p, char *name, void *value, size_t size);
#endif

#endif
