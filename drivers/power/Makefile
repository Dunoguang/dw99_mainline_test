# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_POWER_RESET)	+= reset/
obj-$(CONFIG_POWER_SEQUENCING)	+= sequencing/
obj-$(CONFIG_POWER_SUPPLY)	+= supply/

# ---- SPRD power reset (ported) ----
obj-$(CONFIG_POWER_RESET_SC27XX) += reset_vendor/
obj-$(CONFIG_POWER_RESET_SC2723) += reset_vendor/

# ==== SPRD power drivers (ported from 4.4 vendor tree) ====
obj-$(CONFIG_CHARGER_FAN54015)	+= fan54015.o fan54015_charge.o
obj-$(CONFIG_BATTERY_SPRD)  += sprd_battery.o
obj-$(CONFIG_CHARGER_SC2705)	+= sc2705-charger.o sprd-sc2705-charger.o
obj-$(CONFIG_CHARGER_SC2703)	+= sc2703-charger.o sprd-sc2703-charger.o
obj-$(CONFIG_CHARGER_SC2723)		+= sprd_2723_charge.o
obj-$(CONFIG_CHARGER_SC2721)		+= sprd_2721_charge.o
obj-$(CONFIG_CHARGER_SC2720)		+= sprd_2720_charge.o
obj-$(CONFIG_CHARGER_SC2731) 	+= sprd_2731_charge.o sprdchg_2731_wdt.o
obj-$(CONFIG_CHARGER_SPRD2701)	+= sprd_2701.o sprd_2701_charge.o
obj-$(CONFIG_FGU_SC27XX) 		+= sprd_27xx_fgu.o
obj-$(CONFIG_PMIC_SC2723) 	+= sprd2723_chg_timer.o sprd_charge_2723_helper.o
obj-$(CONFIG_PMIC_SC2731) 	+= sprd2731_chg_timer.o sprd_2731_fchg.o sprd_charge_helper.o
obj-$(CONFIG_PMIC_SC2721)	+= sprd2721_chg_timer.o sprd_2721_fchg.o sprd_charge_2721_helper.o
obj-$(CONFIG_PMIC_SC2720)	+= sprd2721_chg_timer.o sprd_charge_2720_helper.o
obj-$(CONFIG_CHARGER_SPRD2700) += sprd_2700.o sprd_2700_charge.o
