/*copyright (C) 2017 Spreadtrum Communications Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/mfd/syscon/sprd-glb.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/regmap.h>


static unsigned long long  dram_size;
static struct proc_dir_entry *sprd_dmc_proc_dir;
static struct regmap *pmu_apb;
static unsigned int dram_info;
static unsigned int cs0_info;
static unsigned int cs1_info;
static unsigned int pmu_reg;
static unsigned int bit_mask;
static unsigned int bit_value;
static struct regmap *aon_apb;
static unsigned int aon_reg;

#define AON_APB_DDR_SLEEP_CTRL0          aon_reg
#define AON_APB_DDR_SLEEP_CTRL1          (aon_reg + 4)
#define AON_APB_DDR_SLEEP_CTRL2          (aon_reg + 8)
#define AON_APB_CONF_MODE_SEL            BIT(5)
#define AON_APB_DDR_SLP_CTRL_FSM_STATE   (0xF << 12)
#define AON_APB_CONF_NO_SUBSYS_HW_EN     0x80

static int sprd_dmc_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%lld\n", (long long int)(dram_size/1024/1024));
	return 0;
}
static int sprd_dmc_ddr_type_proc_show(struct seq_file *m, void *v)
{
	unsigned int type;

	type = (dram_info & 0x0F);
	switch (type) {
	case 1:
		seq_printf(m, "%s\n", "LPDDR1");
		break;
	case 2:
		seq_printf(m, "%s\n", "LPDDR2");
		break;
	case 3:
		seq_printf(m, "%s\n", "LPDDR3");
		break;
	case 4:
		seq_printf(m, "%s\n", "LPDDR4");
		break;
	case 5:
		seq_printf(m, "%s\n", "LPDDR4x");
		break;
	case 6:
		seq_printf(m, "%s\n", "LPDDR4y");
		break;
	case 7:
		seq_printf(m, "%s\n", "LPDDR5");
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int sprd_dmc_ddr_info_proc_show(struct seq_file *m, void *v)
{
	unsigned int cs_num;

	cs_num = ((dram_info >> 4) & 0x0F);
	seq_printf(m, "cs_num:%d\n", cs_num);
	seq_printf(m, "cs0_mr5:0x%x cs0_mr6:0x%x cs0_mr7:0x%x cs0_mr8:0x%x\n",
		   (cs0_info & 0xff), ((cs0_info >> 8) & 0xff),
		   ((cs0_info >> 16) & 0xff), ((cs0_info >> 24) & 0xff));
	if (cs_num == 2)
		seq_printf(m, "cs1_mr5:0x%x cs1_mr6:0x%x cs1_mr7:0x%x cs1_mr8:0x%x\n",
			   (cs1_info & 0xff), ((cs1_info >> 8) & 0xff),
			   ((cs1_info >> 16) & 0xff), ((cs1_info >> 24) & 0xff));
	return 0;
}

static int sprd_dmc_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, sprd_dmc_proc_show, pde_data(inode));
}
static int sprd_dmc_ddr_type_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, sprd_dmc_ddr_type_proc_show, pde_data(inode));
}

static int sprd_dmc_ddr_info_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, sprd_dmc_ddr_info_proc_show, pde_data(inode));
}

static const struct proc_ops sprd_dmc_fops = {
	.proc_open = sprd_dmc_proc_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};
static const struct proc_ops sprd_dmc_ddr_type_fops = {
	.proc_open = sprd_dmc_ddr_type_proc_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};
static const struct proc_ops sprd_dmc_ddr_info_fops = {
	.proc_open = sprd_dmc_ddr_info_proc_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int dmc_proc_creat(void)
{
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *tmp_proc;

	sprd_dmc_proc_dir = proc_mkdir("sprd_dmc", NULL);
	if (!sprd_dmc_proc_dir)
		return -ENOMEM;

	tmp_proc = proc_create_data("property", S_IRUGO,
			     sprd_dmc_proc_dir, &sprd_dmc_fops, NULL);
	tmp_proc = proc_create_data("ddr_type", S_IRUGO,
			     sprd_dmc_proc_dir, &sprd_dmc_ddr_type_fops, NULL);
	tmp_proc = proc_create_data("ddr_mode_reg", S_IRUGO,
			     sprd_dmc_proc_dir, &sprd_dmc_ddr_info_fops, NULL);
	if (!tmp_proc)
		return -ENOMEM;
#endif
	return 0;
}

void sprd_ddr_force_light_sleep(void)
{
	unsigned int val;

	if (pmu_apb)
		regmap_update_bits(pmu_apb, pmu_reg,
				   bit_mask, bit_value);

	if (aon_apb) {
		regmap_update_bits(aon_apb, AON_APB_DDR_SLEEP_CTRL0,
				AON_APB_CONF_MODE_SEL, 0);
		while (1) {
			regmap_read(aon_apb, AON_APB_DDR_SLEEP_CTRL2, &val);
			if (!(val&AON_APB_DDR_SLP_CTRL_FSM_STATE))
				break;
			cpu_relax();
		}
		regmap_write(aon_apb, AON_APB_DDR_SLEEP_CTRL1,
				AON_APB_CONF_NO_SUBSYS_HW_EN);
		regmap_update_bits(aon_apb, AON_APB_DDR_SLEEP_CTRL0,
				AON_APB_CONF_MODE_SEL, AON_APB_CONF_MODE_SEL);
	}

}

static void ddr_sleep_ctrl_init(void)
{
	struct device_node *np;

	np = of_find_node_by_name(NULL, "sleep-ctrl");
	if (np) {
		aon_apb = syscon_regmap_lookup_by_phandle(np,
							"sprd,sys-aon-apb");
		if (IS_ERR(aon_apb)) {
			pr_err("%s:device node sys-aon-apb is not found.\n",
				__func__);
		} else {
			if (of_property_read_u32_index(np,
					"sprd,aon_reg", 0, &aon_reg)) {
				pr_err("Error: can't read aon reg offset!\n");
			}
		}

		pmu_apb = syscon_regmap_lookup_by_phandle(np,
							  "sprd,sys-pmu-apb");
		if (IS_ERR(pmu_apb)) {
			pr_err("%s:device node sys-pmu-apb is not found.\n",
				__func__);
		} else {
			if (of_property_read_u32_index(np,
					"sprd,pmu_reg", 0, &pmu_reg)) {
				pr_err("Error: can't read pmu reg offset!\n");
			}

			if (of_property_read_u32_index(np,
					"sprd,bit_mask", 0, &bit_mask)) {
				pr_err("Error: can't read bit mask offset!\n");
			}

			if (of_property_read_u32_index(np,
					"sprd,bit_value", 0, &bit_value)) {
				pr_err("Error: can't read bit value offset!\n");
			}
		}
	} else {
		pr_err("%s: device node sleep-ctrl is not found.\n", __func__);
	}
}

static int __init sci_dmc_init(void)
{
	u32 size_l, size_h;
	u32 reg_off_sizel, reg_off_sizeh;
	u32 ddr_cs0_info, ddr_cs1_info, ddr_dram_info;
	struct device_node *np;
	struct regmap *sprd_syscon_dmc;

	dram_size = 0;

	ddr_sleep_ctrl_init();

	np = of_find_compatible_node(NULL, NULL, "sprd,sys-dmc-phy");
	if  (!np) {
		pr_err("Error: device node sprd,sys-dmc-phy is not found.\n");
		return -ENODEV;
	}

	sprd_syscon_dmc = syscon_regmap_lookup_by_compatible(
						"sprd,sys-dmc-phy");
	if (IS_ERR(sprd_syscon_dmc)) {
		pr_err("Error: sprd,sys-dmc-phy remap failed.\n");
		return -ENODEV;
	}

	if (of_property_read_u32_index(np,
			"sprd,sizel_off", 0, &reg_off_sizel)) {
		pr_err("Error: can't read sizel reg offset!\n");
		return -EINVAL;
	}
	if (of_property_read_u32_index(np,
			"sprd,sizeh_off", 0, &reg_off_sizeh)) {
		pr_err("Error: can't read sizeh reg offset!\n");
		return -EINVAL;
	}
	if (of_property_read_u32_index(np,
			"sprd,dram_info", 0, &ddr_dram_info)) {
		pr_err("Error: can't read cs0 reg offset!\n");
		return -EINVAL;
	}
	if (of_property_read_u32_index(np,
			"sprd,cs0_info", 0, &ddr_cs0_info)) {
		pr_err("Error: can't read cs0 reg offset!\n");
		return -EINVAL;
	}
	if (of_property_read_u32_index(np,
			"sprd,cs1_info", 0, &ddr_cs1_info)) {
		pr_err("Error: can't read cs1 reg offset!\n");
		return -EINVAL;
	}
	regmap_read(sprd_syscon_dmc, reg_off_sizel, &size_l);
	regmap_read(sprd_syscon_dmc, reg_off_sizeh, &size_h);
	regmap_read(sprd_syscon_dmc, ddr_dram_info, &dram_info);
	regmap_read(sprd_syscon_dmc, ddr_cs0_info, &cs0_info);
	regmap_read(sprd_syscon_dmc, ddr_cs1_info, &cs1_info);
	dram_size = (((unsigned long long)size_h) << 32) | size_l;

	pr_info("sci_dmc_init: get dram_size  %#llx bytes\n", dram_size);

	dmc_proc_creat();

	return 0;
}

static void __exit sci_dmc_exit(void)
{
}

module_init(sci_dmc_init);
module_exit(sci_dmc_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("spreadtrum");
MODULE_DESCRIPTION("spreadtrum dmc drv");
