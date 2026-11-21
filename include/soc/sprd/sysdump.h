/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SOC_SPRD_SYSDUMP_H
#define _SOC_SPRD_SYSDUMP_H

struct pt_regs;

void sysdump_enter(int enter_id, const char *reason, struct pt_regs *regs);
void sysdump_ipi(struct pt_regs *regs);
void sprd_set_reboot_mode(const char *cmd);
void sprd_ddr_force_light_sleep(void);

#endif
