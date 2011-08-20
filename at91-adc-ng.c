/*
 * Driver for ADC channels on PortuxG20
 *
 * Copyright 2010-2011 (c) Mathias Langer,
 * taskit GmbH - www.taskit.de
 *
 * Initial development funded by :
 * m.i.t GmbH Berlin - www.mit-gmbh.biz
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <asm/uaccess.h>

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/atmel_tc.h>
#include <linux/atmel_pdc.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>
#include <linux/poll.h>

#include <mach/hardware.h>
#include <mach/at91_adc.h>
#include <mach/gpio.h>

#define AT91_ADC_DEV_NAME     "adc"

#define AT91_ADC_DMA_SIZE     (32 * 1024)
#define AT91_ADC_CHANNEL      4
#define AT91_ADC_CLK_HIGHRES  1000000
#define AT91_ADC_CLK_LOWRES   5000000

#ifndef diff
#define diff(a, b)  ((a) > (b) ? (a) - (b) : (b) - (a))
#endif

/* register access macros */
#define at91_adc_write(value, offset)      __raw_writel(value, at91_adc_device.regs + (offset))
#define at91_adc_read(offset)              __raw_readl(at91_adc_device.regs + (offset))
#define at91_trigger_write(value, offset)  __raw_writel(value, at91_adc_device.trigger->regs + (offset))

struct dma_buffer_t {
	void *buffer;
	dma_addr_t address;
};

struct {
	int is_open;
	struct miscdevice miscdev;
	struct clk *mck;
	struct clk *clock;
	void __iomem *regs;               /* adc register mapping */
	struct atmel_tc *trigger;         /* timer used as trigger for adc */
	struct dma_buffer_t buf0;         /* double buffering ... */
	struct dma_buffer_t buf1;
	struct dma_buffer_t *active_buf;  /* currently accessed by dma */
	struct dma_buffer_t *passive_buf; /* completed buffer */
	u32 read_pos;
	u32 buff_size;
	struct {
		int res_10bit;            /* =0 -> 8bit; 10bit else */
		u32 freq;                 /* trigger frequency */
		u8 ch_mask;               /* enable/disable channels */
	} settings;
} at91_adc_device;

static DECLARE_MUTEX(adc_user_lock);
static DECLARE_WAIT_QUEUE_HEAD(adc_wq);
static spinlock_t adc_buffer_lock = SPIN_LOCK_UNLOCKED;

static irqreturn_t adc_rx_handler(int irq, void *dev_id)
{
	volatile u32 status;
	struct dma_buffer_t *tmp;
	u32 dma_size;
	unsigned long flags;

	status = at91_adc_read(AT91_ADC_SR);

	/* set pdc next pointer register */
	if(at91_adc_device.settings.res_10bit == 0)
		dma_size = at91_adc_device.buff_size;
	else
		dma_size = at91_adc_device.buff_size / 2;
	at91_adc_write((u32)at91_adc_device.active_buf->address, ATMEL_PDC_RNPR);
	at91_adc_write(dma_size, ATMEL_PDC_RNCR);

	spin_lock_irqsave(&adc_buffer_lock, flags);

	/* swap buffers */
	tmp = at91_adc_device.active_buf;
	at91_adc_device.active_buf = at91_adc_device.passive_buf;
	at91_adc_device.passive_buf = tmp;
	at91_adc_device.read_pos = 0;

	spin_unlock_irqrestore(&adc_buffer_lock, flags);

	/* wake up readers */
	wake_up_interruptible(&adc_wq);

	return IRQ_HANDLED;
}

static int adc_close(struct inode *inode, struct file *file)
{
	u32 mr;

	if(down_interruptible(&adc_user_lock))
		return -ERESTARTSYS;
	if(at91_adc_device.is_open) {
		/* stop adc */
		at91_adc_write(0xFFFFFFFF, AT91_ADC_IDR);
		mr = at91_adc_read(AT91_ADC_MR);
		mr &= ~AT91_ADC_TRGEN;
		at91_adc_write(mr, AT91_ADC_MR);
		at91_adc_write(0x0F, AT91_ADC_CHDR);

		/* stop pdc */
		at91_adc_write(ATMEL_PDC_RXTDIS, ATMEL_PDC_PTCR);

		/* free irq */
		free_irq(AT91SAM9260_ID_ADC, 0);

		/* disable clocks */
		clk_disable(at91_adc_device.clock);
		clk_disable(at91_adc_device.trigger->clk[0]);
	}

	at91_adc_device.is_open = 0;
	up(&adc_user_lock);
	return 0;
}

static int adc_open(struct inode *inode, struct file *file)
{
	int ret_val;
	u32 freq;
	u32 tmp;
	u32 dma_size;
	int n;
	u8 mask;

	if(down_interruptible(&adc_user_lock))
		return -ERESTARTSYS;

	if(at91_adc_device.is_open) {
		ret_val = -EBUSY;
		goto error;
	}

	at91_adc_device.is_open = 1;

	/* enable adc & trigger clock */
	clk_enable(at91_adc_device.clock);
	clk_enable(at91_adc_device.trigger->clk[0]);

	/* disable interrupt */
	at91_adc_write(0xFFFFFFFF, AT91_ADC_IDR);

	/* calculate appropriate buffer size for desired frequency */
	mask = at91_adc_device.settings.ch_mask;
	n = (mask & 0x01) + ((mask & 0x02) >> 1) +
	    ((mask & 0x04) >> 2) + ((mask & 0x08) >> 3);
	if(at91_adc_device.settings.res_10bit == 1)
		n = n * 2;
	at91_adc_device.buff_size = at91_adc_device.settings.freq * n;
	if(at91_adc_device.buff_size > AT91_ADC_DMA_SIZE)
		at91_adc_device.buff_size = AT91_ADC_DMA_SIZE - (AT91_ADC_DMA_SIZE % n);
	at91_adc_device.read_pos = at91_adc_device.buff_size;

	/* setup pdc & buffers */
	at91_adc_device.active_buf = &at91_adc_device.buf0;
	at91_adc_device.passive_buf = &at91_adc_device.buf1;
	if(at91_adc_device.settings.res_10bit == 0)
		dma_size = at91_adc_device.buff_size;
	else
		dma_size = at91_adc_device.buff_size / 2;
	at91_adc_write((u32)at91_adc_device.buf0.address, ATMEL_PDC_RPR);
	at91_adc_write(dma_size, ATMEL_PDC_RCR);
	at91_adc_write((u32)at91_adc_device.buf1.address, ATMEL_PDC_RNPR);
	at91_adc_write(dma_size, ATMEL_PDC_RNCR);
	at91_adc_write(ATMEL_PDC_RXTEN, ATMEL_PDC_PTCR);

	/* init trigger */
	freq = at91_adc_device.settings.freq;
	at91_trigger_write(ATMEL_TC_CLKDIS, ATMEL_TC_CCR);
	at91_trigger_write(0x0000000F, ATMEL_TC_IDR);
	/* use MCK / 128 as timer base */
	tmp = ATMEL_TC_TIMER_CLOCK4;
	/* set TIOA at RA compare; clear at RC compare */
	tmp |= ATMEL_TC_WAVESEL_UP_AUTO | ATMEL_TC_WAVE | ATMEL_TC_ACPA_SET | ATMEL_TC_ACPC_CLEAR;
	at91_trigger_write(tmp, ATMEL_TC_CMR);
	/* setup trigger frequency */
	tmp = clk_get_rate(at91_adc_device.mck) / 128 / freq;
	if(tmp > 0xFFFF)
		tmp = 0xFFFF;
	at91_trigger_write(tmp / 2, ATMEL_TC_RA);
	at91_trigger_write(tmp, ATMEL_TC_RC);
	at91_trigger_write(0, ATMEL_TC_CV);
	/* store frequency really used */
	freq = clk_get_rate(at91_adc_device.mck) / 128 / tmp;
	at91_adc_device.settings.freq = freq;

	/* init adc */
	at91_adc_write(AT91_ADC_SWRST, AT91_ADC_CR);
	/* setup clock */
	if(at91_adc_device.settings.res_10bit)
		tmp = clk_get_rate(at91_adc_device.mck) / (2 * AT91_ADC_CLK_HIGHRES);
	else
		tmp = clk_get_rate(at91_adc_device.mck) / (2 * AT91_ADC_CLK_LOWRES);
	if(tmp > 0) tmp--;
	if(tmp > 0x3F) tmp = 0x3F;
	tmp = AT91_ADC_PRESCAL_(tmp);
	/* resolution */
	if(at91_adc_device.settings.res_10bit == 0)
		tmp |= AT91_ADC_LOWRES;
	/* select Timer0 as trigger for conversions */
	tmp |= AT91_ADC_TRGSEL_TC0;
	tmp |= AT91_ADC_TRGEN;
	at91_adc_write(tmp, AT91_ADC_MR);
	/* setup handler */
	if(request_irq(AT91SAM9260_ID_ADC, &adc_rx_handler, 0, "at91-adc", NULL)) {
		ret_val = -EBUSY;
		goto error;
	}
	/* enable interrupt */
	at91_adc_write(AT91_ADC_ENDRX, AT91_ADC_IER);
	/* enable channels */
	at91_adc_write(at91_adc_device.settings.ch_mask, AT91_ADC_CHER);

	/* start trigger */
	at91_trigger_write(ATMEL_TC_CLKEN, ATMEL_TC_CCR);
	at91_trigger_write(ATMEL_TC_SWTRG, ATMEL_TC_CCR);

	up(&adc_user_lock);
	return 0;

error:
	up(&adc_user_lock);
	adc_close(inode, file);
	return ret_val;
}

static ssize_t adc_read(struct file *file, char __user *buffer, size_t count, loff_t *offset)
{
	size_t n;
	ssize_t ret_val;
	unsigned long flags;
	void* adc_buffer;
	u32 adc_pos;

	if(count <= 0)
		return 0;

	if(down_interruptible(&adc_user_lock))
		return -ERESTARTSYS;

	/* check for available data to read */
	if(at91_adc_device.read_pos >= at91_adc_device.buff_size) {
		up(&adc_user_lock);
		/* non-blocking */
		if(file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		/* blocking */
		if(wait_event_interruptible(adc_wq, (at91_adc_device.read_pos == 0)))
			return -ERESTARTSYS;
		if(down_interruptible(&adc_user_lock))
			return -ERESTARTSYS;
	}

	/* get current output buffer & position */
	spin_lock_irqsave(&adc_buffer_lock, flags);
	adc_buffer = at91_adc_device.passive_buf->buffer;
	adc_pos = at91_adc_device.read_pos;
	spin_unlock_irqrestore(&adc_buffer_lock, flags);

	n = at91_adc_device.buff_size - at91_adc_device.read_pos;
	if(count < n)
		n = count;

	/* copy data to user space */
	if(n > 0) {
		if(copy_to_user(buffer, adc_buffer + adc_pos, n) != 0) {
			ret_val = -EFAULT;
			goto end;
		}
		/* update current readposition if not changed by interrupt */
		spin_lock_irqsave(&adc_buffer_lock, flags);
		if(at91_adc_device.read_pos == adc_pos)
			at91_adc_device.read_pos = adc_pos + n;
		spin_unlock_irqrestore(&adc_buffer_lock, flags);
	}
	ret_val = n;

end:
	up(&adc_user_lock);
	return ret_val;
}

static unsigned int adc_poll(struct file *file, poll_table *wait)
{
	unsigned int mask = 0;

	down(&adc_user_lock);
	poll_wait(file, &adc_wq, wait);
	/* check for data available */
	if(at91_adc_device.read_pos < at91_adc_device.buff_size)
		mask = POLLIN | POLLRDNORM;
	up(&adc_user_lock);
	return mask;
}

static void free_buffers(void)
{
	if(at91_adc_device.regs != NULL)
		iounmap(at91_adc_device.regs);
	if(at91_adc_device.buf0.buffer != NULL)
		dma_free_coherent(NULL, AT91_ADC_DMA_SIZE,
		                  at91_adc_device.buf0.buffer,
		                  at91_adc_device.buf0.address);
	if(at91_adc_device.buf1.buffer != NULL)
		dma_free_coherent(NULL, AT91_ADC_DMA_SIZE,
		                  at91_adc_device.buf1.buffer,
		                  at91_adc_device.buf1.address);
}

static struct file_operations adc_fops = {
	.owner   = THIS_MODULE,
	.open    = adc_open,
	.release = adc_close,
	.read    = adc_read,
	.poll    = adc_poll,
};

/* en/disables a channel by manipulating the corresponding
   bit in channel mask */
static inline void switch_channel(u32 channel, u32 state)
{
	at91_adc_device.settings.ch_mask &= ~(1 << channel);
	at91_adc_device.settings.ch_mask |= ((state > 0) << channel);
}

/* determines whether a channel is enabled (return != 0)
   or not (return 0) */
static inline u32 channel_enabled(u32 channel)
{
	return (at91_adc_device.settings.ch_mask & (1 << channel)) > 0;
}

/* read a single value */
static u16 read_value(u32 channel)
{
	void* adc_buffer;
	unsigned int ch_pos, i;
	u32 tmp;
	u16 value;

	/* channel disabled -> return 0 */
	if(!channel_enabled(channel))
		return 0;

	if(at91_adc_device.is_open) {
		/* determine channel position */
		ch_pos = 0;
		for(i = 0; i < channel; i++)
			if(channel_enabled(i))
				ch_pos++;
		/* return channel's value from passive buffer */
		adc_buffer = at91_adc_device.passive_buf->buffer;
		if(at91_adc_device.settings.res_10bit)
			value = ((u16*)adc_buffer)[ch_pos];
		else
			value = ((u8*)adc_buffer)[ch_pos];
	}
	else {
		/* setup adc */
		clk_enable(at91_adc_device.clock);
		at91_adc_write(AT91_ADC_SWRST, AT91_ADC_CR);
		/* setup clock */
		if(at91_adc_device.settings.res_10bit)
			tmp = clk_get_rate(at91_adc_device.mck) / AT91_ADC_CLK_HIGHRES;
		else
			tmp = clk_get_rate(at91_adc_device.mck) / AT91_ADC_CLK_LOWRES;
		if(tmp > 0) tmp--;
		if(tmp > 0x3F) tmp = 0x3F;
		tmp = AT91_ADC_PRESCAL_(tmp);
		/* resolution */
		if(at91_adc_device.settings.res_10bit == 0)
			tmp |= AT91_ADC_LOWRES;
		at91_adc_write(tmp, AT91_ADC_MR);
		/* enable selected channel */
		at91_adc_write(1 << channel, AT91_ADC_CHER);

		/* read value */
		at91_adc_write(AT91_ADC_START, AT91_ADC_CR);
		while((at91_adc_read(AT91_ADC_SR) & AT91_ADC_DRDY) == 0);
		value = at91_adc_read(AT91_ADC_LCDR);

		/* disable adc */
		at91_adc_write(0x0F, AT91_ADC_CHDR);
		at91_adc_write(AT91_ADC_SWRST, AT91_ADC_CR);
		clk_disable(at91_adc_device.clock);
	}
	return value;
}

/* sysfs read callback */
static ssize_t adc_attr_read(struct device *device, struct device_attribute *attr, char *buf)
{
	char channel_name[16];
	ssize_t ret_val, channel;
	u32 value = 0;

	if(down_interruptible(&adc_user_lock))
		return -ERESTARTSYS;
	if(strcmp(attr->attr.name, "highres") == 0)
		value = (at91_adc_device.settings.res_10bit > 0);
	else if(strcmp(attr->attr.name, "frequency") == 0)
		value = at91_adc_device.settings.freq;
	else {
		for(channel = 0; channel < AT91_ADC_CHANNEL; channel++) {
			/* check for ch?_enable */
			if(snprintf(channel_name, sizeof(channel_name), "ch%d_enable", channel) <= 0)
				break;
			if(strcmp(attr->attr.name, channel_name) == 0) {
				value = channel_enabled(channel);
				break;
			}
			/* ... ch?_value */
			if(snprintf(channel_name, sizeof(channel_name), "ch%d_value", channel) <= 0)
				break;
			if(strcmp(attr->attr.name, channel_name) == 0) {
				value = read_value(channel);
				break;
			}
		}
	}
	ret_val = snprintf(buf, PAGE_SIZE, "%d\n", value);
	up(&adc_user_lock);
	return ret_val + 1;
}

/* sysfs write callback */
static ssize_t adc_attr_write(struct device *device, struct device_attribute *attr, const char *buf, size_t size)
{
	char channel_name[16];
	ssize_t channel;
	u32 value = 0;

	if(at91_adc_device.is_open)
		return -EBUSY;

	value = simple_strtoul(buf, NULL, 0);

	if(down_interruptible(&adc_user_lock))
		return -ERESTARTSYS;
	if(strcmp(attr->attr.name, "highres") == 0)
		at91_adc_device.settings.res_10bit = (value > 0);
	else if(strcmp(attr->attr.name, "frequency") == 0) {
		if(value > 0)
			at91_adc_device.settings.freq = value;
	}
	else {
		for(channel = 0; channel < AT91_ADC_CHANNEL; channel++) {
			/* check for ch?_enable */
			if(snprintf(channel_name, sizeof(channel_name), "ch%d_enable", channel) <= 0)
				break;
			if(strcmp(attr->attr.name, channel_name) == 0) {
				switch_channel(channel, value);
				break;
			}
		}
	}
	up(&adc_user_lock);

	return size;
}

static struct device_attribute adc_res_attr =
	__ATTR(highres, 0666, adc_attr_read, adc_attr_write);

static struct device_attribute adc_freq_attr =
	__ATTR(frequency, 0666, adc_attr_read, adc_attr_write);

static struct device_attribute adc_ch0_attr =
	__ATTR(ch0_enable, 0666, adc_attr_read, adc_attr_write);
static struct device_attribute adc_ch0_value =
	__ATTR(ch0_value, 0444, adc_attr_read, NULL);

static struct device_attribute adc_ch1_attr =
	__ATTR(ch1_enable, 0666, adc_attr_read, adc_attr_write);
static struct device_attribute adc_ch1_value =
	__ATTR(ch1_value, 0444, adc_attr_read, NULL);

static struct device_attribute adc_ch2_attr =
	__ATTR(ch2_enable, 0666, adc_attr_read, adc_attr_write);
static struct device_attribute adc_ch2_value =
	__ATTR(ch2_value, 0444, adc_attr_read, NULL);

static struct device_attribute adc_ch3_attr =
	__ATTR(ch3_enable, 0666, adc_attr_read, adc_attr_write);
static struct device_attribute adc_ch3_value =
	__ATTR(ch3_value, 0444, adc_attr_read, NULL);

static void cleanup(void)
{
	/* stop and free trigger */
	if(at91_adc_device.trigger != NULL)
		atmel_tc_free(at91_adc_device.trigger);

	if(at91_adc_device.mck != NULL)
		clk_put(at91_adc_device.mck);

	if(at91_adc_device.clock != NULL)
		clk_put(at91_adc_device.clock);

	misc_deregister(&at91_adc_device.miscdev);

	free_buffers();
}

static int __init adc_init(void)
{
	int ret_val;

	/* init device structure */
	memset(&at91_adc_device, 0, sizeof(at91_adc_device));
	at91_adc_device.miscdev.minor = MISC_DYNAMIC_MINOR;
	at91_adc_device.miscdev.name = AT91_ADC_DEV_NAME;
	at91_adc_device.miscdev.fops = &adc_fops;
	if(misc_register(&at91_adc_device.miscdev) != 0) {
		ret_val = -EINVAL;
		goto error;
	}

	/* create sysfs files */
	if(device_create_file(at91_adc_device.miscdev.this_device, &adc_res_attr) != 0 ||
	   device_create_file(at91_adc_device.miscdev.this_device, &adc_freq_attr) !=0 ||
	   device_create_file(at91_adc_device.miscdev.this_device, &adc_ch0_attr) !=0 ||
	   device_create_file(at91_adc_device.miscdev.this_device, &adc_ch0_value) !=0 ||
	   device_create_file(at91_adc_device.miscdev.this_device, &adc_ch1_attr) !=0 ||
	   device_create_file(at91_adc_device.miscdev.this_device, &adc_ch1_value) !=0 ||
	   device_create_file(at91_adc_device.miscdev.this_device, &adc_ch2_attr) !=0 ||
	   device_create_file(at91_adc_device.miscdev.this_device, &adc_ch2_value) !=0 ||
	   device_create_file(at91_adc_device.miscdev.this_device, &adc_ch3_attr) !=0 ||
	   device_create_file(at91_adc_device.miscdev.this_device, &adc_ch3_value) !=0) {
		ret_val = -EINVAL;
		goto error;
	}

	/* get clocks */
	at91_adc_device.mck = clk_get(NULL, "mck");
	if(IS_ERR(at91_adc_device.mck)) {
		ret_val = -EINVAL;
		goto error;
	}

	at91_adc_device.clock = clk_get(NULL, "adc_clk");
	if(IS_ERR(at91_adc_device.clock)) {
		ret_val = -EINVAL;
		goto error;
	}

	/* map adc registers */
	at91_adc_device.regs = ioremap(AT91SAM9260_BASE_ADC, 0x0200);
	if(at91_adc_device.regs == NULL) {
		ret_val = -EINVAL;
		goto error;
	}

	/* allocate dma buffers */
	at91_adc_device.buf0.buffer =
		dma_alloc_coherent(NULL, AT91_ADC_DMA_SIZE,
		                   &at91_adc_device.buf0.address, GFP_KERNEL);
	at91_adc_device.buf1.buffer =
		dma_alloc_coherent(NULL, AT91_ADC_DMA_SIZE,
		                   &at91_adc_device.buf1.address, GFP_KERNEL);
	if((at91_adc_device.buf0.buffer == NULL) ||
	   (at91_adc_device.buf0.buffer == NULL)) {
		ret_val = -ENOMEM;
		goto error;
	}

	/* request timer */
	at91_adc_device.trigger = atmel_tc_alloc(0, AT91_ADC_DEV_NAME);
	if(at91_adc_device.trigger == NULL) {
		ret_val = -EBUSY;
		goto error;
	}

	/* default settings */
	at91_adc_device.settings.res_10bit = 1;
	at91_adc_device.settings.freq = 1000;
	at91_adc_device.settings.ch_mask = 0x0F;

	return 0;

error:
	cleanup();
	return ret_val;
}

static void __exit adc_exit(void)
{
	cleanup();
}

module_init(adc_init);
module_exit(adc_exit);

MODULE_AUTHOR("Mathias Langer");
MODULE_DESCRIPTION("PortuxG20 ADC driver");
MODULE_LICENSE("GPL");
