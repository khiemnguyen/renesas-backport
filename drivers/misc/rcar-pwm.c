/*
 * Pulse Width Modulation(PWM) for R-Car
 *
 * Copyright (C) 2014 Atmark Techno, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/pwm.h>
#include <mach/hardware.h>
#include <linux/delay.h>

#define PWMCR	0x00   /* PWM control register */
#define 	PWMCR_CC0_MASK		(0xf0000)
#define 	PWMCR_CC0_SHIFT		(16)
#define 	PWMCR_CCMD		(1<<15)
#define 	PWMCR_CC_MAX		(24)
#define 	PWMCR_SYNC		(1<<11)
#define 	PWMCR_SS0		(1<<4)
#define 	PWMCR_EN0		(1<<0)
#define PWMCNT	0x04   /* PWM count register */
#define 	PWMCNT_CYC0_MASK	(0x03ff0000)
#define 	PWMCNT_PH0_MASK		(0x000003ff)
#define 	PWMCNT_CYC_MAX		(0x3ff)

struct pwm_device {
	struct list_head	node;
	struct platform_device *pdev;

	const char	*label;
	struct clk	*clk;

	void __iomem	*base;

	u32 pwmcr;
	u32 pwmcnt;

	unsigned int	use_count;
	unsigned int	pwm_id;
};

int pwm_config(struct pwm_device *pwm, int duty_ns, int period_ns)
{
	unsigned long long clk;
	unsigned long period_cycles, duty_cycles;
	u32 div, cc0, ccmd;

	if (!pwm || duty_ns < 0 || period_ns <= 0 || duty_ns > period_ns)
		return -EINVAL;

	for (div = 0; div <= PWMCR_CC_MAX; div++) {
		clk = clk_get_rate(pwm->clk);
		do_div(clk, 1 << div);
		clk *= period_ns;
		do_div(clk, 1000000000);
		if (clk <= PWMCNT_CYC_MAX)
			break;
	}

	if (div > PWMCR_CC_MAX) {
		printk("too large period: %d(ns)\n", period_ns);
		return -EINVAL;
	}
	period_cycles = (clk > 1) ? clk : 2;
	duty_cycles = (period_cycles * duty_ns / period_ns) ?: 1;

	cc0 = div / 2;
	ccmd = div % 2;

	pwm->pwmcr = (cc0 << 16) | (ccmd << 15);
	pwm->pwmcnt = (period_cycles << 16) | duty_cycles;

	return 0;
}
EXPORT_SYMBOL(pwm_config);

static void rcar_pwm_update(struct pwm_device *pwm)
{
	writel(pwm->pwmcnt, pwm->base + PWMCNT);
	writel(pwm->pwmcr | PWMCR_EN0 | PWMCR_SYNC,
	       pwm->base + PWMCR);
}

int pwm_enable(struct pwm_device *pwm)
{
	rcar_pwm_update(pwm);

	return 0;
}
EXPORT_SYMBOL(pwm_enable);

void pwm_disable(struct pwm_device *pwm)
{
	/* Cannot set 0% duty ratio */
	pwm->pwmcnt = (PWMCNT_CYC0_MASK | 1);
	pwm->pwmcr = 0;

	rcar_pwm_update(pwm);
}
EXPORT_SYMBOL(pwm_disable);

static DEFINE_MUTEX(pwm_lock);
static LIST_HEAD(pwm_list);

struct pwm_device *pwm_request(int pwm_id, const char *label)
{
	struct pwm_device *pwm;
	int found = 0;

	mutex_lock(&pwm_lock);

	list_for_each_entry(pwm, &pwm_list, node) {
		if (pwm->pwm_id == pwm_id) {
			found = 1;
			break;
		}
	}

	if (found) {
		if (pwm->use_count == 0) {
			pwm->use_count++;
			pwm->label = label;
		} else
			pwm = ERR_PTR(-EBUSY);
	} else
		pwm = ERR_PTR(-ENOENT);

	mutex_unlock(&pwm_lock);
	return pwm;
}
EXPORT_SYMBOL(pwm_request);

void pwm_free(struct pwm_device *pwm)
{
	mutex_lock(&pwm_lock);

	if (pwm->use_count) {
		pwm->use_count--;
		pwm->label = NULL;
	} else
		pr_warning("PWM device already freed\n");

	mutex_unlock(&pwm_lock);
}
EXPORT_SYMBOL(pwm_free);

static int __devinit rcar_pwm_probe(struct platform_device *pdev)
{
	struct pwm_device *pwm;
	struct resource *res;
	int ret = 0;

	pwm = kzalloc(sizeof(struct pwm_device), GFP_KERNEL);
	if (!pwm) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	pwm->clk = clk_get(&pdev->dev, "pwm-rcar");
	if (IS_ERR(pwm->clk)) {
		dev_err(&pdev->dev, "failed to clk_get\n");
		ret = PTR_ERR(pwm->clk);
		goto err_free;
	}

	pwm->use_count = 0;
	pwm->pwm_id = pdev->id;
	pwm->pdev = pdev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "no memory resource defined\n");
		ret = -ENODEV;
		goto err_free_clk;
	}

	res = request_mem_region(res->start, resource_size(res), pdev->name);
	if (!res) {
		dev_err(&pdev->dev, "failed to request memory resource\n");
		ret = -EBUSY;
		goto err_free_clk;
	}

	pwm->base = ioremap(res->start, resource_size(res));
	if (!pwm->base) {
		dev_err(&pdev->dev, "failed to ioremap() registers\n");
		ret = -ENODEV;
		goto err_free_mem;
	}

	mutex_lock(&pwm_lock);
	if (list_empty(&pwm_list))
		clk_enable(pwm->clk);
	list_add_tail(&pwm->node, &pwm_list);
	mutex_unlock(&pwm_lock);

	platform_set_drvdata(pdev, pwm);

	dev_info(&pdev->dev, "registering PWM%d\n", pwm->pwm_id);

	return 0;

err_free_mem:
	release_mem_region(res->start, resource_size(res));
err_free_clk:
	clk_put(pwm->clk);
err_free:
	kfree(pwm);
	return ret;
}

static int __devexit rcar_pwm_remove(struct platform_device *pdev)
{
	struct pwm_device *pwm;
	struct resource *res;

	pwm = platform_get_drvdata(pdev);
	if (!pwm)
		return -ENODEV;

	mutex_lock(&pwm_lock);
	list_del(&pwm->node);
	if (list_empty(&pwm_list))
		clk_disable(pwm->clk);
	mutex_unlock(&pwm_lock);

	iounmap(pwm->base);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(res->start, resource_size(res));

	clk_put(pwm->clk);

	kfree(pwm);
	return 0;
}

static struct platform_driver rcar_pwm_driver = {
	.probe		= rcar_pwm_probe,
	.remove		= __devexit_p(rcar_pwm_remove),
	.driver		= {
		.name	= "pwm-rcar",
		.owner = THIS_MODULE,
	},
};

static int __init rcar_pwm_init(void)
{
	return platform_driver_register(&rcar_pwm_driver);
}
subsys_initcall(rcar_pwm_init);

static void __exit rcar_pwm_exit(void)
{
	platform_driver_unregister(&rcar_pwm_driver);
}
module_exit(rcar_pwm_exit);

MODULE_AUTHOR("Daisuke Mizobuchi");
MODULE_LICENSE("GPL v2");
