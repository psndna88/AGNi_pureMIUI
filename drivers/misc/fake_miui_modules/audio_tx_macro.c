/*
 * AGNi Fake MIUI R modules
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>

static int fake_miui_sound_probe(struct platform_device *pdev)
{
	return 0;
}

static int fake_miui_sound_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver fake_miui_sound = {
	.driver	= {
		.name	= "fake-miui-sound",
	},
	.probe		= fake_miui_sound_probe,
	.remove		= fake_miui_sound_remove,
};

module_platform_driver(fake_miui_sound);

MODULE_AUTHOR("psndna88 <psndna88@gmail.com>");
MODULE_DESCRIPTION("AGNi fake MIUI modules");
MODULE_LICENSE("GPL");
MODULE_ALIAS("techpack:audio");
