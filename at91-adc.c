/* 
 *  Driver for ADC on the FoxBoardG20
 *
 *  Copyright (R) 2010 - Claudio Mignanti
 *
 *  Based on http://www.at91.com/forum/viewtopic.php/p,9409/#p9409
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as  published by 
 *  the Free Software Foundation.
 *
 * $Id$
 * ---------------------------------------------------------------------------
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/fs.h>
#include <linux/gpio.h>

#include <asm/uaccess.h>
#include <asm/io.h>

#include "at91_adc.h"
#include "at91_tc.h"


#define ADC_MAJOR 178
#define ADC_NAME "at91-adc"

#define ADC_REQUEST 1
#define ADC_READ 2


void __iomem *adc_base;
struct clk *adc_clk;


/* Function to read last conversion of channel 0*/
static int read_chan (int chan)
{
	int val;
	/* TODO: support for chan */
	__raw_writel(0x02, adc_base + AT91_ADC_CR);	//Start the ADC
	while(!__raw_readl(adc_base + AT91_ADC_EOC(chan))) 			//Is conversion ready?
		cpu_relax();
	val= __raw_readl(adc_base + AT91_ADC_CHR(chan)); //Read & Return the conversion
	
	return val;
}

/* 	PC0  -> AD0
	PC1	AD1
	PC2	AD2
	PC3	AD3 */
static int request_chan (int chan) {

	int pin_chan;

	switch (chan) { 
		case 0:
			pin_chan=AT91_PIN_PC0;
			break;
		case 1:
			pin_chan=AT91_PIN_PC1;
			break;
		case 2:
			pin_chan=AT91_PIN_PC2;
			break;
		case 3:
			pin_chan=AT91_PIN_PC3;
			break;
		default:
			return -EINVAL;
	}

	at91_set_A_periph(pin_chan, 0);				//Mux PIN to GPIO

	adc_base = ioremap(AT91SAM9260_BASE_ADC, SZ_16K);	//Map the mem region
	__raw_writel(0x01, adc_base + AT91_ADC_CR);		//Reset the ADC
	__raw_writel(( AT91_ADC_SHTIM | AT91_ADC_STARTUP | AT91_ADC_PRESCAL | \
		AT91_ADC_SLEEP | AT91_ADC_LOWRES | AT91_ADC_TRGSEL | \
		AT91_ADC_TRGEN), adc_base + AT91_ADC_MR);	//Mode setup

	//__raw_writel(CH_EN, adc_base + AT91_ADC_CHER);	???	//Enable Channels
	//__raw_writel(CH_DIS, adc_base + AT91_ADC_CHDR);		//Disable Channels

	return 0;
}

// ioctl - I/O control
static int adc_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg) {
	 int retval = 0;
	 switch ( cmd ) {
		case ADC_REQUEST:
			return request_chan ((int)arg);
			break;
		case ADC_READ:
			return read_chan ((int)arg);
			break;
		default:
			retval = -EINVAL;
	}
	return retval;
}

struct file_operations adc_fops = {
	.owner =	THIS_MODULE,
	.ioctl =	adc_ioctl,
};

/* Module Cleanup Function */
static void adc_exit(void)
{
	clk_disable(adc_clk); 		//Turn off ADC clock
	iounmap(adc_base);		//Unmap the ADC mem region
	unregister_chrdev (ADC_MAJOR, ADC_NAME); //Free the major,minor numbers
}

/* Module initialization function */
static int adc_init(void)
{
	int result;

	/* ADC Set Up */
	adc_clk = clk_get(NULL, "adc_clk");
	clk_enable(adc_clk);

	/*Get dynamic major number and a minor number */
	result = register_chrdev(ADC_MAJOR, ADC_NAME, &adc_fops);
	if(result < 0){
		printk(KERN_WARNING "adc: can't get major %d\n", ADC_MAJOR);
		return result;
	}

	return 0;
}


module_init(adc_init);
module_exit(adc_exit);

MODULE_AUTHOR("Paul Kavan");
MODULE_AUTHOR("Claudio Mignanti");
MODULE_DESCRIPTION("ADC Driver for the FoxBoardG20");
MODULE_LICENSE("GPL");
