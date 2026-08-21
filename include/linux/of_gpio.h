/* SPDX-License-Identifier: GPL-2.0 */
/* DW99 port: 4.4 of_gpio.h legacy shim (of_get_gpio) */
#ifndef _LINUX_OF_GPIO_H
#define _LINUX_OF_GPIO_H

#include <linux/of.h>
#include <linux/of_address.h>

static inline int of_get_gpio(struct device_node *np, int index)
{
	struct device_node *gpio_np;
	u32 gpio_cells = 1;
	u32 vals[2];
	int ret;

	gpio_np = of_parse_phandle(np, "gpios", index);
	if (!gpio_np)
		return -ENODEV;
	of_property_read_u32(gpio_np, "#gpio-cells", &gpio_cells);
	ret = of_property_read_u32_array(np, "gpios", vals,
					 (index + 1) * gpio_cells);
	if (ret)
		return ret;
	if (gpio_cells == 2)
		return vals[index * 2];
	return vals[index];
}

static inline int of_get_named_gpio(struct device_node *np,
				    const char *propname, int index)
{
	struct device_node *gpio_np;
	u32 gpio_cells = 1;
	u32 vals[2];
	int ret;

	gpio_np = of_parse_phandle(np, propname, index);
	if (!gpio_np)
		return -ENODEV;
	of_property_read_u32(gpio_np, "#gpio-cells", &gpio_cells);
	ret = of_property_read_u32_array(np, propname, vals,
					 (index + 1) * gpio_cells);
	if (ret)
		return ret;
	if (gpio_cells == 2)
		return vals[index * 2];
	return vals[index];
}

#endif
