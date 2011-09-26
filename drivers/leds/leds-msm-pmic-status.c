/*
 * leds-msm-pmic-status.c - MSM PMIC LEDs driver.
 *
 * Copyright (c) 2009, ZTE, Corporation. All rights reserved.
 *
 11/05/10	 lhx  LHX_PM_20101216_01 no need to renew NLEDS state when resume
 11/05/10	 lhx LHX_PM_20101105_01 add code to support NLED to blink after APP enters suspend,maroc CONFIG_ZTE_NLED_BLINK_WHILE_APP_SUSPEND
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/timer.h>
#include <linux/ctype.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <mach/pmic.h>

#define ZYF_BL_TAG "[ZYF@pmic-leds]"

#define MAX_PMIC_BL_LEVEL	16
#define BLINK_LED_NUM   3


bool RED_ON=0;
bool GREEN_ON=0;
bool AMBER_ON=0;

struct BLINK_LED_data{
       int blink_flag;
	int blink_led_flag;  // 0: off, 1:0n
	int blink_on_time;  //ms
	int blink_off_time;  //ms
	struct timer_list timer;
	struct work_struct work_led_on;
	struct work_struct work_led_off;
	struct led_classdev led;
};

struct STATUS_LED_data {
	spinlock_t data_lock;
	struct BLINK_LED_data blink_led[3];  /*green, red, amber */
};

struct STATUS_LED_data *STATUS_LED;

static void pmic_red_led_on(struct work_struct *work)
{
       struct BLINK_LED_data *b_led = container_of(work, struct BLINK_LED_data, work_led_on);
       if(AMBER_ON) STATUS_LED->blink_led[1].led.brightness = 0;
	pmic_set_led_intensity(LED_RED, b_led->led.brightness / MAX_PMIC_BL_LEVEL);
	RED_ON=1;
}

static void pmic_red_led_off(struct work_struct *work)
{
	if(AMBER_ON) STATUS_LED->blink_led[1].led.brightness = 32;
	else pmic_set_led_intensity(LED_RED, LED_OFF);

	RED_ON=0;
}

static void pmic_green_led_on(struct work_struct *work)
{
	struct BLINK_LED_data *b_led = container_of(work, struct BLINK_LED_data, work_led_on);
	if(AMBER_ON) STATUS_LED->blink_led[0].led.brightness = 0;
	pmic_set_led_intensity(LED_GREEN, b_led->led.brightness / MAX_PMIC_BL_LEVEL);
	GREEN_ON=1;
}

static void pmic_green_led_off(struct work_struct *work)
{
	if(AMBER_ON) STATUS_LED->blink_led[0].led.brightness = 32;
	else pmic_set_led_intensity(LED_GREEN, LED_OFF);
	GREEN_ON=0;
}

static void pmic_amber_led_on(struct work_struct *work)
{
       struct BLINK_LED_data *b_led = container_of(work, struct BLINK_LED_data, work_led_on);
	pmic_set_led_intensity(LED_RED, b_led->led.brightness / MAX_PMIC_BL_LEVEL);
	pmic_set_led_intensity(LED_GREEN, b_led->led.brightness / MAX_PMIC_BL_LEVEL);
	AMBER_ON=1;
}

static void pmic_amber_led_off(struct work_struct *work)
{
	if(!RED_ON) pmic_set_led_intensity(LED_RED, LED_OFF);
	if(!GREEN_ON) pmic_set_led_intensity(LED_GREEN, LED_OFF);
	AMBER_ON=0;
}

static void (*func[6])(struct work_struct *work) = {
	pmic_red_led_on,
	pmic_red_led_off,
	pmic_green_led_on,
	pmic_green_led_off,
	pmic_amber_led_on,
	pmic_amber_led_off,
};

static void msm_pmic_bl_led_set(struct led_classdev *led_cdev,
	enum led_brightness value)
{
	int ret;
	
	if (!strcmp(led_cdev->name, "red")) {
		ret = pmic_set_led_intensity(LED_RED, value / MAX_PMIC_BL_LEVEL);
	} else if (!strcmp(led_cdev->name, "green")) {
		ret = pmic_set_led_intensity(LED_GREEN, value / MAX_PMIC_BL_LEVEL);
	} else {
		ret = pmic_set_led_intensity(LED_RED, value / MAX_PMIC_BL_LEVEL);
		ret += pmic_set_led_intensity(LED_GREEN, value / MAX_PMIC_BL_LEVEL);
	}
	
	if (ret)
		dev_err(led_cdev->dev, "[ZYF@PMIC LEDS]can't set pmic backlight\n");
}

static void pmic_leds_timer(unsigned long arg)
{
      struct BLINK_LED_data *b_led = (struct BLINK_LED_data *)arg;


		if(b_led->blink_flag)
		{
		       if(b_led->blink_led_flag)
		       {
		              schedule_work(&b_led->work_led_off);
		       	b_led->blink_led_flag = 0;  
		       	mod_timer(&b_led->timer,jiffies + msecs_to_jiffies(b_led->blink_off_time));
		       }
			else
			{
			       schedule_work(&b_led->work_led_on);
		       	b_led->blink_led_flag = 1;
				mod_timer(&b_led->timer,jiffies + msecs_to_jiffies(b_led->blink_on_time));
		       }
		}	
		else
		{
		       schedule_work(&b_led->work_led_on);
		}
		
}

static ssize_t led_blink_solid_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct STATUS_LED_data *STATUS_LED;
	int idx = 1;
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	ssize_t ret = 0;

	if (!strcmp(led_cdev->name, "red"))
		idx = 0;
	else if (!strcmp(led_cdev->name, "green"))
		idx = 1;
	else
		idx = 2;

	STATUS_LED = container_of(led_cdev, struct STATUS_LED_data, blink_led[idx].led);

	/* no lock needed for this */
	sprintf(buf, "%u\n", STATUS_LED->blink_led[idx].blink_flag);
	ret = strlen(buf) + 1;

	return ret;
}

static ssize_t led_blink_solid_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t size)
{
	struct STATUS_LED_data *STATUS_LED;
	int idx = 1;
	char *after;
	unsigned long state;
	ssize_t ret = -EINVAL;
	size_t count;

	struct led_classdev *led_cdev = dev_get_drvdata(dev);

	if (!strcmp(led_cdev->name, "red"))
		idx = 0;
	else if (!strcmp(led_cdev->name, "green"))
		idx = 1;
	else
		idx = 2;

	STATUS_LED = container_of(led_cdev, struct STATUS_LED_data, blink_led[idx].led);

	state = simple_strtoul(buf, &after, 10);

	count = after - buf;

	if (*after && isspace(*after))
		count++;

	if (count == size) {
		ret = count;
		spin_lock(&STATUS_LED->data_lock);
		if(0==state)
		{
		       STATUS_LED->blink_led[idx].blink_flag= 0;
			pr_info(ZYF_BL_TAG "DISABLE %d led blink \n",idx);
		}
		else
		{
		       STATUS_LED->blink_led[idx].blink_flag= 1;
		       STATUS_LED->blink_led[idx].blink_led_flag = 0;
			schedule_work(&STATUS_LED->blink_led[idx].work_led_off);
			mod_timer(&STATUS_LED->blink_led[idx].timer,jiffies + msecs_to_jiffies(STATUS_LED->blink_led[idx].blink_off_time));
			pr_info(ZYF_BL_TAG "ENABLE %d led blink \n",idx);
		}
		spin_unlock(&STATUS_LED->data_lock);
	}

	return ret;
}

static DEVICE_ATTR(blink, 0644, led_blink_solid_show, led_blink_solid_store);

static ssize_t cpldled_grpfreq_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct STATUS_LED_data *STATUS_LED;
	int idx = 1;

	struct led_classdev *led_cdev = dev_get_drvdata(dev);

	if (!strcmp(led_cdev->name, "red"))
		idx = 0;
	else if (!strcmp(led_cdev->name, "green"))
		idx = 1;
	else
		idx = 2;

	STATUS_LED = container_of(led_cdev, struct STATUS_LED_data, blink_led[idx].led);
	return sprintf(buf, "blink_on_time = %u ms \n", STATUS_LED->blink_led[idx].blink_on_time);
}

static ssize_t cpldled_grpfreq_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t size)
{
	struct STATUS_LED_data *STATUS_LED;
	int idx = 1;
	char *after;
	unsigned long state;
	ssize_t ret = -EINVAL;
	size_t count;

	struct led_classdev *led_cdev = dev_get_drvdata(dev);

	if (!strcmp(led_cdev->name, "red"))
		idx = 0;
	else if (!strcmp(led_cdev->name, "green"))
		idx = 1;
	else
		idx = 2;

	STATUS_LED = container_of(led_cdev, struct STATUS_LED_data, blink_led[idx].led);

	state = simple_strtoul(buf, &after, 10);

	count = after - buf;

	if (*after && isspace(*after))
		count++;

	if (count == size) {
		ret = count;
		spin_lock(&STATUS_LED->data_lock);
		STATUS_LED->blink_led[idx].blink_on_time = state;  //in ms
		pr_info(ZYF_BL_TAG "Set %d led blink_on_time=%d ms \n",idx,STATUS_LED->blink_led[idx].blink_on_time);
		spin_unlock(&STATUS_LED->data_lock);
	}

	return ret;
}

static DEVICE_ATTR(grpfreq, 0644, cpldled_grpfreq_show, cpldled_grpfreq_store);

static ssize_t cpldled_grppwm_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct STATUS_LED_data *STATUS_LED;
	int idx = 1;

	struct led_classdev *led_cdev = dev_get_drvdata(dev);

	if (!strcmp(led_cdev->name, "red"))
		idx = 0;
	else if (!strcmp(led_cdev->name, "green"))
		idx = 1;
	else
		idx = 2;

	STATUS_LED = container_of(led_cdev, struct STATUS_LED_data, blink_led[idx].led);
	return sprintf(buf, "blink_off_time = %u ms\n", STATUS_LED->blink_led[idx].blink_off_time);
}

static ssize_t cpldled_grppwm_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size)
{
	struct STATUS_LED_data *STATUS_LED;
	int idx = 1;
	char *after;
	unsigned long state;
	ssize_t ret = -EINVAL;
	size_t count;

	struct led_classdev *led_cdev = dev_get_drvdata(dev);

	if (!strcmp(led_cdev->name, "red"))
		idx = 0;
	else if (!strcmp(led_cdev->name, "green"))
		idx = 1;
	else
		idx = 2;

	STATUS_LED = container_of(led_cdev, struct STATUS_LED_data, blink_led[idx].led);

	state = simple_strtoul(buf, &after, 10);

	count = after - buf;

	if (*after && isspace(*after))
		count++;

	if (count == size) {
		ret = count;
		spin_lock(&STATUS_LED->data_lock);
		STATUS_LED->blink_led[idx].blink_off_time= state;  //in ms
		pr_info(ZYF_BL_TAG "Set %d led blink_off_time=%d ms \n",idx,STATUS_LED->blink_led[idx].blink_off_time);
		spin_unlock(&STATUS_LED->data_lock);
	}

	return ret;
}

static DEVICE_ATTR(grppwm, 0644, cpldled_grppwm_show, cpldled_grppwm_store);

static int msm_pmic_led_probe(struct platform_device *pdev)
{
	int ret = 0;
	int i, j;

	STATUS_LED = kzalloc(sizeof(struct STATUS_LED_data), GFP_KERNEL);
	if (STATUS_LED == NULL) {
		printk(KERN_ERR "STATUS_LED_probe: no memory for device\n");
		ret = -ENOMEM;
		goto err_alloc_failed;
	}
	
	STATUS_LED->blink_led[0].led.name = "red";
	STATUS_LED->blink_led[0].led.brightness_set = msm_pmic_bl_led_set;
	STATUS_LED->blink_led[0].led.brightness = LED_OFF;
	STATUS_LED->blink_led[0].blink_flag = 0;
	STATUS_LED->blink_led[0].blink_on_time = 500;
	STATUS_LED->blink_led[0].blink_off_time = 500;

	STATUS_LED->blink_led[1].led.name = "green";
	STATUS_LED->blink_led[1].led.brightness_set = msm_pmic_bl_led_set;
	STATUS_LED->blink_led[1].led.brightness = LED_OFF;
	STATUS_LED->blink_led[1].blink_flag = 0;
	STATUS_LED->blink_led[1].blink_on_time = 500;
	STATUS_LED->blink_led[1].blink_off_time = 500;

	STATUS_LED->blink_led[2].led.name = "amber";
	STATUS_LED->blink_led[2].led.brightness_set = msm_pmic_bl_led_set;
	STATUS_LED->blink_led[2].led.brightness = LED_OFF;
	STATUS_LED->blink_led[2].blink_flag = 0;
	STATUS_LED->blink_led[2].blink_on_time = 500;
	STATUS_LED->blink_led[2].blink_off_time = 500;

	spin_lock_init(&STATUS_LED->data_lock);

	for (i = 0; i < 3; i++) {	/* red, green, amber */
		ret = led_classdev_register(&pdev->dev, &STATUS_LED->blink_led[i].led);
		if (ret) {
			printk(KERN_ERR
			       "STATUS_LED: led_classdev_register failed\n");
			goto err_led_classdev_register_failed;
		}
	}

	for (i = 0; i < 3; i++) {
		ret =
		    device_create_file(STATUS_LED->blink_led[i].led.dev, &dev_attr_blink);
		if (ret) {
			printk(KERN_ERR
			       "STATUS_LED: create dev_attr_blink failed\n");
			goto err_out_attr_blink;
		}
	}

	for (i = 0; i < 3; i++) {
		ret =
		    device_create_file(STATUS_LED->blink_led[i].led.dev, &dev_attr_grppwm);
		if (ret) {
			printk(KERN_ERR
			       "STATUS_LED: create dev_attr_grppwm failed\n");
			goto err_out_attr_grppwm;
		}
	}

	for (i = 0; i < 3; i++) {
		ret =
		    device_create_file(STATUS_LED->blink_led[i].led.dev, &dev_attr_grpfreq);
		if (ret) {
			printk(KERN_ERR
			       "STATUS_LED: create dev_attr_grpfreq failed\n");
			goto err_out_attr_grpfreq;
		}
	}
	dev_set_drvdata(&pdev->dev, STATUS_LED);
	
	for (i = 0; i < 3; i++)
	{
	       INIT_WORK(&STATUS_LED->blink_led[i].work_led_on, func[i*2]);
	       INIT_WORK(&STATUS_LED->blink_led[i].work_led_off, func[i*2+1]);
	       setup_timer(&STATUS_LED->blink_led[i].timer, pmic_leds_timer, (unsigned long)&STATUS_LED->blink_led[i]);
		msm_pmic_bl_led_set(&STATUS_LED->blink_led[i].led, LED_OFF);
	}
	
       return 0;
	   
err_out_attr_grpfreq:
	for (j = 0; j < i; j++)
		device_remove_file(STATUS_LED->blink_led[i].led.dev, &dev_attr_blink);
	i = 2;
	
err_out_attr_grppwm:
	for (j = 0; j < i; j++)
		device_remove_file(STATUS_LED->blink_led[i].led.dev, &dev_attr_blink);
	i = 2;
	
err_out_attr_blink:
	for (j = 0; j < i; j++)
		device_remove_file(STATUS_LED->blink_led[i].led.dev, &dev_attr_blink);
	i = 2;

err_led_classdev_register_failed:
	for (j = 0; j < i; j++)
		led_classdev_unregister(&STATUS_LED->blink_led[i].led);

err_alloc_failed:
	kfree(STATUS_LED);

	return ret;
	
}

static int __devexit msm_pmic_led_remove(struct platform_device *pdev)
{
       int i;
	   
       for (i = 0; i < 2; i++)
		led_classdev_unregister(&STATUS_LED->blink_led[i].led);

	return 0;
}

#ifndef CONFIG_ZTE_NLED_BLINK_WHILE_APP_SUSPEND	//LHX_NLED_20110107_01 enable NLED to blink for all projects
#define CONFIG_ZTE_NLED_BLINK_WHILE_APP_SUSPEND
#endif

#ifdef CONFIG_ZTE_NLED_BLINK_WHILE_APP_SUSPEND
#include "../../arch/arm/mach-msm/proc_comm.h"
#define NLED_APP2SLEEP_FLAG_LCD 0x0001//green
#define NLED_APP2SLEEP_FLAG_KBD 0x0010//red
//#define NLED_APP2SLEEP_FLAG_VIB 0x0100
#define ZTE_PROC_COMM_CMD3_NLED_BLINK_DISABLE 2
#define ZTE_PROC_COMM_CMD3_NLED_BLINK_ENABLE 3

//inform modem the states of NLED through PCOM_CUSTOMER_CMD2
int msm_pmic_led_config_while_app2sleep(unsigned blink_kbd,unsigned blink_lcd, unsigned set)
{
	int config_last = 0;
	if(blink_kbd > 0)
	{
		config_last |= NLED_APP2SLEEP_FLAG_KBD;
	}
	if(blink_lcd > 0)
	{
		config_last |= NLED_APP2SLEEP_FLAG_LCD;
	}
	pr_info("LHX LED PROC:Green %d,RED%d;config 0x%x,set(2dis 3enable):%d\n",blink_lcd,blink_kbd,config_last,set);
//	return msm_proc_comm(PCOM_CUSTOMER_CMD2, &config_last, &set);
	return msm_proc_comm(PCOM_CUSTOMER_CMD3, &config_last, &set);
}
#endif

#ifdef CONFIG_PM
static int msm_pmic_led_suspend(struct platform_device *dev,
		pm_message_t state)
{
       int i;
	   #ifdef CONFIG_ZTE_NLED_BLINK_WHILE_APP_SUSPEND
	   //blink_led[0] red,blink_led[1] green
	   msm_pmic_led_config_while_app2sleep( STATUS_LED->blink_led[0].led.brightness,//red
	   STATUS_LED->blink_led[1].led.brightness, ZTE_PROC_COMM_CMD3_NLED_BLINK_ENABLE);//green
	   #endif

	pr_crit(ZYF_BL_TAG "Set leds for suspend");

#ifdef CONFIG_LED_SUSPEND_IMMEDIATE
	for (i = 0; i < 3; i++) {
		led_classdev_suspend(&STATUS_LED->blink_led[i].led);
	}
#elif defined(CONFIG_LED_SUSPEND_SOLID) || defined(CONFIG_LED_SUSPEND_SHORT)
	//we set the leds for suspend, but don't do anything on resume. We should restore the previous state at resume but since it looks like android resets the leds at resume it shouldn't matter.
	//red led sticks in gsf, green led blinks for notifications, so only check green
	if(STATUS_LED->blink_led[1].blink_flag > 0) {
		pr_crit(ZYF_BL_TAG "Set led %d on for suspend",i);
		STATUS_LED->blink_led[1].blink_flag=0;
		STATUS_LED->blink_led[1].led.brightness=16;
	}
	msleep(1000); //wait for LED to turn on
#endif

	return 0;
}

static int msm_pmic_led_resume(struct platform_device *dev)
{
	#ifdef CONFIG_ZTE_NLED_BLINK_WHILE_APP_SUSPEND
	   msm_pmic_led_config_while_app2sleep( 0,0, ZTE_PROC_COMM_CMD3_NLED_BLINK_DISABLE);
	#endif

#if defined(CONFIG_LED_SUSPEND_IMMEDIATE) || defined(CONFIG_LED_SUSPEND_SHORT)
	int i;

	for (i = 0; i < 3; i++)
		 led_classdev_resume(&STATUS_LED->blink_led[i].led);
#endif
	return 0;
}
#else
#define msm_pmic_led_suspend NULL
#define msm_pmic_led_resume NULL
#endif

static struct platform_driver msm_pmic_led_driver = {
	.probe		= msm_pmic_led_probe,
	.remove		= __devexit_p(msm_pmic_led_remove),
	.suspend	= msm_pmic_led_suspend,
	.resume		= msm_pmic_led_resume,
	.driver		= {
		.name	= "pmic-leds-status",
		.owner	= THIS_MODULE,
	},
};

static int __init msm_pmic_led_init(void)
{
	return platform_driver_register(&msm_pmic_led_driver);
}
module_init(msm_pmic_led_init);

static void __exit msm_pmic_led_exit(void)
{
	platform_driver_unregister(&msm_pmic_led_driver);
}
module_exit(msm_pmic_led_exit);

MODULE_DESCRIPTION("MSM PMIC LEDs driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:pmic-leds");
