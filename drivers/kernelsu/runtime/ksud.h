#ifndef __KSU_H_KSUD
#define __KSU_H_KSUD

#include <linux/compat.h>
#include <asm/syscall.h>
#include "compat/kernel_compat.h"

#define KSUD_PATH "/data/adb/ksud"

void ksu_ksud_init(void);
void ksu_ksud_exit(void);

void ksu_handle_execveat_ksud(char *filename, struct user_arg_ptr *argv, struct user_arg_ptr *envp, int *flags);
void ksu_stop_ksud_execve_hook(void);
#ifdef CONFIG_KSU_TRACEPOINT_HOOK
void ksu_execve_hook_ksud(const struct pt_regs *regs);
void ksu_execveat_hook_ksud(const struct pt_regs *regs);
#endif
void ksu_stop_input_hook_runtime(void);

void ksu_handle_vfs_fstat(int fd, loff_t *kstat_size_ptr);
void ksu_handle_initrc(struct file *file);
int ksu_handle_sys_read(unsigned int fd, char __user **buf_ptr, size_t *count_ptr);
int ksu_handle_input_handle_event(unsigned int *type, unsigned int *code, int *value);

#endif
