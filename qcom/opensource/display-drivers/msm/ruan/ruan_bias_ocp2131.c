// SPDX-License-Identifier: GPL-2.0-only
/*
 * LCD bias for ruan: OCP2131 (qcom,ocp2131_bias) with four control GPIOs.
 *
 * Xiaomi never published this driver; on 5.10 it is built into their
 * msm_drm module. Same behaviour: at probe the positive and negative
 * outputs (registers 0 and 1) are set to 0x13; on panel power on the
 * GPIOs go high in the order cs, iovdd, enp, enn, 5 ms apart, before the
 * panel regulators; on power off they go low in reverse order after the
 * regulators.
 */
#define pr_fmt(fmt)	"ocp2131: " fmt
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_gpio.h>

#include "ruan_bias_ocp2131.h"

#define OCP2131_VOLTAGE		0x13

enum { BIAS_CS, BIAS_IOVDD, BIAS_ENP, BIAS_ENN, BIAS_GPIO_NUM };

static const char * const ocp2131_gpio_names[BIAS_GPIO_NUM] = {
	[BIAS_CS]	= "bias,lcm-cs-gpio",
	[BIAS_IOVDD]	= "bias,lcm-iovdd-gpio",
	[BIAS_ENP]	= "bias,lcm-enp-gpio",
	[BIAS_ENN]	= "bias,lcm-enn-gpio",
};

struct ocp2131 {
	struct i2c_client *client;
	int gpio[BIAS_GPIO_NUM];
};

static struct ocp2131 *ocp2131_dev;
static DEFINE_MUTEX(ocp2131_lock);

static void ocp2131_set(int idx, int val)
{
	if (gpio_is_valid(ocp2131_dev->gpio[idx]))
		gpio_direction_output(ocp2131_dev->gpio[idx], val);
}

void ruan_bias_enable(void)
{
	mutex_lock(&ocp2131_lock);
	if (ocp2131_dev) {
		ocp2131_set(BIAS_CS, 1);
		mdelay(5);
		ocp2131_set(BIAS_IOVDD, 1);
		mdelay(5);
		ocp2131_set(BIAS_ENP, 1);
		mdelay(5);
		ocp2131_set(BIAS_ENN, 1);
	}
	mutex_unlock(&ocp2131_lock);
}

void ruan_bias_disable(void)
{
	mutex_lock(&ocp2131_lock);
	if (ocp2131_dev) {
		mdelay(5);
		ocp2131_set(BIAS_ENN, 0);
		mdelay(5);
		ocp2131_set(BIAS_ENP, 0);
		mdelay(5);
		ocp2131_set(BIAS_IOVDD, 0);
		mdelay(5);
		ocp2131_set(BIAS_CS, 0);
	}
	mutex_unlock(&ocp2131_lock);
}

static void ocp2131_write(struct i2c_client *client, u8 reg)
{
	int ret = i2c_smbus_write_byte_data(client, reg, OCP2131_VOLTAGE);

	if (ret < 0)
		dev_err(&client->dev, "write 0x%02x to reg 0x%02x failed: %d\n",
			OCP2131_VOLTAGE, reg, ret);
}

static int ocp2131_probe(struct i2c_client *client)
{
	struct device_node *np = client->dev.of_node;
	struct ocp2131 *ocp;
	int i, ret;

	ocp = devm_kzalloc(&client->dev, sizeof(*ocp), GFP_KERNEL);
	if (!ocp)
		return -ENOMEM;
	ocp->client = client;

	for (i = 0; i < BIAS_GPIO_NUM; i++) {
		ocp->gpio[i] = of_get_named_gpio(np, ocp2131_gpio_names[i], 0);
		if (!gpio_is_valid(ocp->gpio[i])) {
			dev_err(&client->dev, "no %s\n", ocp2131_gpio_names[i]);
			continue;
		}
		ret = devm_gpio_request(&client->dev, ocp->gpio[i],
					ocp2131_gpio_names[i] + 5);
		if (ret)
			dev_err(&client->dev, "request %s failed: %d\n",
				ocp2131_gpio_names[i], ret);
	}

	/* The bootloader leaves the bias on; program the output voltages */
	ocp2131_write(client, 0x00);
	ocp2131_write(client, 0x01);

	mutex_lock(&ocp2131_lock);
	ocp2131_dev = ocp;
	mutex_unlock(&ocp2131_lock);

	dev_info(&client->dev, "LCD bias ready\n");
	return 0;
}

static void ocp2131_remove(struct i2c_client *client)
{
	mutex_lock(&ocp2131_lock);
	ocp2131_dev = NULL;
	mutex_unlock(&ocp2131_lock);
}

static const struct of_device_id ocp2131_match_table[] = {
	{ .compatible = "qcom,ocp2131_bias" },
	{ },
};

static const struct i2c_device_id ocp2131_id[] = {
	{ "ocp2131_bias", 0 },
	{ },
};

static struct i2c_driver ocp2131_driver = {
	.driver = {
		.name = "ocp2131_bias",
		.of_match_table = ocp2131_match_table,
	},
	.id_table = ocp2131_id,
	.probe = ocp2131_probe,
	.remove = ocp2131_remove,
};

int ruan_bias_ocp2131_init(void)
{
	return i2c_add_driver(&ocp2131_driver);
}

void ruan_bias_ocp2131_exit(void)
{
	i2c_del_driver(&ocp2131_driver);
}
