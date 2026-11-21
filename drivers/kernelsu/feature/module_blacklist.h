#ifndef __KSU_MODULE_BLACKLIST_H
#define __KSU_MODULE_BLACKLIST_H

void ksu_module_blacklist_init(const char *modules);
void ksu_module_blacklist_exit();

void ksu_handle_init_module(int *result);
void ksu_handle_finit_module(int *result);

#endif