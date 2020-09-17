// SPDX-License-Identifier: GPL-2.0
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include "clk.h"

#define CCM_CCDR			0x4
#define CCDR_MMDC_CH0_MASK		BIT(17)
#define CCDR_MMDC_CH1_MASK		BIT(16)

DEFINE_SPINLOCK(imx_ccm_lock);

void imx_unregister_clocks(struct clk *clks[], unsigned int count)
{
	unsigned int i;

	for (i = 0; i < count; i++)
		clk_unregister(clks[i]);
}

void imx_unregister_hw_clocks(struct clk_hw *hws[], unsigned int count)
{
	unsigned int i;

	for (i = 0; i < count; i++)
		clk_hw_unregister(hws[i]);
}

void __init imx_mmdc_mask_handshake(void __iomem *ccm_base,
				    unsigned int chn)
{
	unsigned int reg;

	reg = readl_relaxed(ccm_base + CCM_CCDR);
	reg |= chn == 0 ? CCDR_MMDC_CH0_MASK : CCDR_MMDC_CH1_MASK;
	writel_relaxed(reg, ccm_base + CCM_CCDR);
}

void imx_check_clocks(struct clk *clks[], unsigned int count)
{
	unsigned i;

	for (i = 0; i < count; i++)
		if (IS_ERR(clks[i]))
			pr_err("i.MX clk %u: register failed with %ld\n",
			       i, PTR_ERR(clks[i]));
}

void imx_check_clk_hws(struct clk_hw *clks[], unsigned int count)
{
	unsigned int i;

	for (i = 0; i < count; i++)
		if (IS_ERR(clks[i]))
			pr_err("i.MX clk %u: register failed with %ld\n",
			       i, PTR_ERR(clks[i]));
}

static struct clk * __init imx_obtain_fixed_clock_from_dt(const char *name)
{
	struct of_phandle_args phandle;
	struct clk *clk = ERR_PTR(-ENODEV);
	char *path;

	path = kasprintf(GFP_KERNEL, "/clocks/%s", name);
	if (!path)
		return ERR_PTR(-ENOMEM);

	phandle.np = of_find_node_by_path(path);
	kfree(path);

	if (phandle.np) {
		clk = of_clk_get_from_provider(&phandle);
		of_node_put(phandle.np);
	}
	return clk;
}

struct clk * __init imx_obtain_fixed_clock(
			const char *name, unsigned long rate)
{
	struct clk *clk;

	clk = imx_obtain_fixed_clock_from_dt(name);
	if (IS_ERR(clk))
		clk = imx_clk_fixed(name, rate);
	return clk;
}

struct clk_hw * __init imx_obtain_fixed_clock_hw(
			const char *name, unsigned long rate)
{
	struct clk *clk;

	clk = imx_obtain_fixed_clock_from_dt(name);
	if (IS_ERR(clk))
		clk = imx_clk_fixed(name, rate);
	return __clk_get_hw(clk);
}

struct clk_hw * imx_obtain_fixed_clk_hw(struct device_node *np,
					const char *name)
{
	struct clk *clk;

	clk = of_clk_get_by_name(np, name);
	if (IS_ERR(clk))
		return ERR_PTR(-ENOENT);

	return __clk_get_hw(clk);
}

/*
 * This fixups the register CCM_CSCMR1 write value.
 * The write/read/divider values of the aclk_podf field
 * of that register have the relationship described by
 * the following table:
 *
 * write value       read value        divider
 * 3b'000            3b'110            7
 * 3b'001            3b'111            8
 * 3b'010            3b'100            5
 * 3b'011            3b'101            6
 * 3b'100            3b'010            3
 * 3b'101            3b'011            4
 * 3b'110            3b'000            1
 * 3b'111            3b'001            2(default)
 *
 * That's why we do the xor operation below.
 */
#define CSCMR1_FIXUP	0x00600000

void imx_cscmr1_fixup(u32 *val)
{
	*val ^= CSCMR1_FIXUP;
	return;
}

static int imx_keep_uart_clocks;
static struct clk ** const *imx_uart_clocks;

static int __init imx_keep_uart_clocks_param(char *str)
{
	imx_keep_uart_clocks = 1;

	return 0;
}
__setup_param("earlycon", imx_keep_uart_earlycon,
	      imx_keep_uart_clocks_param, 0);
__setup_param("earlyprintk", imx_keep_uart_earlyprintk,
	      imx_keep_uart_clocks_param, 0);

void imx_register_uart_clocks(struct clk ** const clks[])
{
	if (imx_keep_uart_clocks) {
		int i;

		imx_uart_clocks = clks;
		for (i = 0; imx_uart_clocks[i]; i++)
			clk_prepare_enable(*imx_uart_clocks[i]);
	}
}

static int __init imx_clk_disable_uart(void)
{
	if (imx_keep_uart_clocks && imx_uart_clocks) {
		int i;

		for (i = 0; imx_uart_clocks[i]; i++)
			clk_disable_unprepare(*imx_uart_clocks[i]);
	}

	return 0;
}
late_initcall_sync(imx_clk_disable_uart);
