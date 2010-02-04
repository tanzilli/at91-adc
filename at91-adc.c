/* Driver for ADC on the AT91SAM9260-EK
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/arch/gpio.h>
#include <asm/arch/at91_tc.h>

#include "at91_adc.h"

#define MAX_SAMPLES 128000
#define CASE1 1
#define CASE2 2
#define CASE3 3


void __iomem *adc_base;
struct clk *adc_clk;
struct clk *tc1_clk;
void __iomem *tc1_base;

static struct work_struct adc_wq;
int dex, rate;
unsigned char *adc_val;
DECLARE_COMPLETION(acquisition_complete);


//Adc Work censored Tasklet
static void adc_do_tasklet(struct work_struct *work)
{
  int clk_tic;

  clk_enable(tc1_clk); //Power the timer 
  dex = 0;
  while(!at91_get_gpio_value(AT91_PIN_PA30) && (dex < MAX_SAMPLES))
  {
    __raw_writel(AT91_TC_CLKEN | AT91_TC_SWTRG, tc1_base + AT91_TC_CCR);
    clk_tic = __raw_readl(tc1_base + AT91_TC_CV);
    while(clk_tic < rate)//6250 for 8k
      clk_tic = __raw_readl(tc1_base + AT91_TC_CV);   
    adc_val[dex] = channel0_read();
    dex++;
  }
  clk_disable(tc1_clk); 
  complete(&acquisition_complete);
}

//Interrupt Handler for Button 3
static irqreturn_t btn3_int(int irq, void *dev_id)
{
  if (at91_get_gpio_value(AT91_PIN_PA30) == 0)
  {
    at91_set_gpio_value(AT91_PIN_PA6,0); //Turn LED on
    schedule_work(&adc_wq);
  }
  else
  {
    at91_set_gpio_value(AT91_PIN_PA6,1); //Turn LED off
  }

  return IRQ_HANDLED;
}

/* Function to read last conversion of channel 0*/
static inline unsigned char channel0_read(void)
{   
  __raw_writel(0x02, adc_base + ADC_CR); //Start the ADC
  while(!at91_adc_drdy()) //Is conversion ready?
    cpu_relax();
  return __raw_readl(adc_base + ADC_CDR0); //Read & Return the conversion
}

/* Function to check if DRDY is set */
static inline int at91_adc_drdy(void)
{
  int drdy;
  drdy = __raw_readl(adc_base + ADC_SR);
  if (drdy & 0x1)
    return 1;
  else
    return 0;
}

/* ADC Open */
static int adc_open(struct inode *inode, struct file *filp)
{
  return 0;
}

/* ADC Release */
static int adc_release(struct inode *inode, struct file *filp)
{
  return 0;
}

/* ADC Read */
static ssize_t adc_read(struct file *filp, char __iomem *buf,
    size_t count, loff_t *f_pos)
{
  if (count < sizeof(adc_val))
    return -EINVAL;

  /* Wait for workqueue to signal data is ready */
  wait_for_completion(&acquisition_complete);
  if (copy_to_user(buf, adc_val, dex * sizeof(unsigned char)))
      return -EFAULT;
  return dex;
}

// ioctl - I/O control
static int adc_ioctl(struct inode *inode, struct file *file,
      unsigned int cmd, unsigned long arg) {
   int retval = 0;
   switch ( cmd ) {
          case CASE1:/* 10K sample rate*/
            rate = 5000;
            break;
          case CASE2:/* 16K sample rate*/
            rate = 3125;
            break;
          case CASE3:
            rate = 1563;
            break;
          default:
            retval = -EINVAL;
   }
   return retval;
}

struct file_operations adc_fops = {
  .owner =      THIS_MODULE,
  .read =       adc_read,
  .open =       adc_open,
  .ioctl =   adc_ioctl,
  .release =    adc_release,
};

/* Module Cleanup Function */
static void adc_exit(void)
{
  clk_disable(adc_clk); //Turn off ADC clock
  clk_put(adc_clk);
  iounmap(adc_base);    //Unmap the ADC mem region
  unregister_chrdev (ADC_MAJOR, ADC_NAME); //Free the major,minor numbers
  free_irq(AT91_PIN_PA30, NULL); //Free the interrupt
  kfree(adc_val);
}

/* Module initialization function */
static int adc_init(void)
{
  int result;
  adc_val = (unsigned char *) kmalloc(MAX_SAMPLES, GFP_KERNEL);

  /* ADC Set Up */
  adc_clk = clk_get(NULL, "adc_clk"); //Start ADC Clock
  clk_enable(adc_clk);
  at91_set_A_periph(AT91_PIN_PC0,0); //Mux ADC0 to GPIO
  adc_base = ioremap(AT91SAM9260_BASE_ADC,SZ_16K); //Map the mem region
  __raw_writel(0x01, adc_base + ADC_CR); //Reset the ADC
  __raw_writel((SHTIM << 24 | STARTUP << 16 | PRESCAL << 8 | SLEEP_MODE << 5 |
        LOWRES << 4 | TRGSEL << 1 | TRGEN), adc_base + ADC_MR); //Mode setup
  __raw_writel(CH_EN, adc_base + ADC_CHER);     //Enable Channels
  __raw_writel(CH_DIS, adc_base + ADC_CHDR);    //Disable Channels

  //Get dynamic major number and a minor number
  result = register_chrdev(ADC_MAJOR, ADC_NAME, &adc_fops);
  if(result < 0){
    printk(KERN_WARNING "adc: can't get major %d\n", ADC_MAJOR);
    return result;
  }

  /* Set up of other devices around ADC for user i/o */
  at91_set_gpio_input(AT91_PIN_PA30,1); //Set user button 3 as input
  at91_set_gpio_output(AT91_PIN_PA6,1); //Set user LED as output
  at91_set_deglitch(AT91_PIN_PA30,1); //Set glitch filter on button 3
  //Request IRQ's for the button
  if (request_irq(AT91_PIN_PA30, btn3_int, 0, "btn3", NULL))
               return -EBUSY;

  //Initialize the work censored
  INIT_WORK(&adc_wq, adc_do_tasklet);

   /* Set up Timer Counter 0 */
  tc1_clk = clk_get(NULL, "tc1_clk");
  tc1_base = ioremap(AT91SAM9260_BASE_TC1,64); //Map the mem region
  __raw_writel(0x01, tc1_base + AT91_TC_CCR);

  return 0;
}

module_init(adc_init);
module_exit(adc_exit);

MODULE_AUTHOR("Paul Kavan");
MODULE_DESCRIPTION("ADC Driver for the Demo0");
MODULE_LICENSE("GPL");
