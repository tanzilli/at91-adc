/* ----------------------------------------------------------------------------
 * adc_direct_io_c_example - Example Program for AriaG25 board to read adc vals
 *                     through AriaG25 Port C 
 *
 * Copyright (c) 2012-2013 Maurizio Scarpa.
 * All rights reserved.
 *
 * adc_direct_io_c_example is based on the work of Douglas Gilbert for its mem2io.c
 *        for accessing input output register of the CPU from userspace  
 *        compile with: gcc adc.c  -o adc
 *		  run with:     ./adc
 * 
 * http://www.acmesystems.it
 * Maurizio Scarpa - scarpam72@gmail.com
 *
 * DISCLAIMER: THIS SOFTWARE IS PROVIDED "AS IS" IN THE SAME 
 * TERMS OF THE ORIGINAL DISCLAIMER LISTED BELOW.
 * PLAYING DIRECTLY WITH CPU REGISTER CAN RESULT IN UNPREDICTABLE RESULTS
 * AND CAN EVEN RESULT IN DAMAGE OF THE CPU AND/OR THE ATTACHED HARDWARE.
 * ----------------------------------------------------------------------------
 */

/*
 * Copyright (c) 2010-2012 Douglas Gilbert.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */


#include <stdio.h>     
#include <stdlib.h>     
#include <ctype.h>     
#include <string.h>     
#include <unistd.h>    
#include <sys/mman.h>    
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <math.h>

#define DEV_MEM "/dev/mem"
#define MAP_SIZE 4096   /* needs to be a power of 2! */
#define MAP_MASK (MAP_SIZE - 1)

// addresses of interesting physical Port C registers 
#define ADC_CR    0xF804C000   // ( W) ADC Control Register
#define ADC_MR    0xF804C004   // ( W) ADC Mode Register
#define ADC_SEQR1 0xF804C008   // (RW) ADC Channel Sequence 1 Register
#define ADC_SEQR2 0xF804C00C   // (RW) ADC Channel Sequence 1 Register
#define ADC_CHER  0xF804C010   // ( W) ADC Channel Enable Register
#define ADC_CHDR  0xF804C014   // ( W) ADC Channel Disable Register
#define ADC_CHSR  0xF804C018   // (R ) ADC Channel Status Register
#define ADC_LCDR  0xF804C020   // (R ) ADC Last Converted Data Register
#define ADC_IER   0xF804C024   // ( W) ADC Interrupt Enable Register
#define ADC_IDR   0xF804C028   // ( W) ADC Interrupt Disable Register
#define ADC_IMR   0xF804C02C   // (R ) ADC Interrupt Mask Register
#define ADC_ISR   0xF804C030   // (R ) ADC Interrupt Status Register
#define ADC_OVER  0xF804C03C   // (R ) ADC Overrun Status Register
#define ADC_EMR   0xF804C040   // (RW) ADC Extended Mode Register
#define ADC_CWR   0xF804C044   // (RW) ADC Compare Window Register
#define ADC_CDR0  0xF804C050   // (RW) ADC Channel Data Register
#define ADC_CDR1  0xF804C054   // (RW) ADC Channel Data Register
#define ADC_CDR2  0xF804C058   // (RW) ADC Channel Data Register
#define ADC_CDR3  0xF804C05C   // (RW) ADC Channel Data Register
#define ADC_CDR4  0xF804C060   // (RW) ADC Channel Data Register
#define ADC_CDR5  0xF804C064   // (RW) ADC Channel Data Register
#define ADC_CDR6  0xF804C068   // (RW) ADC Channel Data Register
#define ADC_CDR7  0xF804C06C   // (RW) ADC Channel Data Register
#define ADC_CDR8  0xF804C070   // (RW) ADC Channel Data Register
#define ADC_CDR9  0xF804C074   // (RW) ADC Channel Data Register
#define ADC_CDR10 0xF804C078   // (RW) ADC Channel Data Register
#define ADC_CDR11 0xF804C07C   // (RW) ADC Channel Data Register
#define ADC_TRGR  0xF804C0C0   // (RW) ADC Trigger Register
#define PMC_PCER  0xFFFFFC10   // ( W) PMC Peripheral Clock Enable Register -> 1 Enables the corresponding peripheral clock.


// first map to address 0xF804Cxxx registers
int mem_fd;
void * mmap_ptr;
int verbose = 0;
off_t mask_addr;

// second map to address 0xFFFFFxxx registers
void * mmap_ptr2;
off_t mask_addr2;


// variables to store the mapped address of the interesting registers
void * mapped_ADC_CR;
void * mapped_ADC_MR;
void * mapped_ADC_SEQR1;
void * mapped_ADC_SEQR2;
void * mapped_ADC_CHER;
void * mapped_ADC_CHDR;
void * mapped_ADC_CHSR;
void * mapped_ADC_LCDR;
void * mapped_ADC_IER;
void * mapped_ADC_IDR;
void * mapped_ADC_IMR;
void * mapped_ADC_ISR;
void * mapped_ADC_OVER;
void * mapped_ADC_EMR;
void * mapped_ADC_CWR;
void * mapped_ADC_CDR0;
void * mapped_ADC_CDR1;
void * mapped_ADC_CDR2;
void * mapped_ADC_CDR3;
void * mapped_ADC_CDR4;
void * mapped_ADC_CDR5;
void * mapped_ADC_CDR6;
void * mapped_ADC_CDR7;
void * mapped_ADC_CDR8;
void * mapped_ADC_CDR9;
void * mapped_ADC_CDR10;
void * mapped_ADC_CDR11;
void * mapped_ADC_TRGR;
void * mapped_PMC_PCER;

init_memoryToIO(void) {
	// to map in a local page the peripheral address registers used 
	mem_fd = -1;

	if ((mem_fd = open(DEV_MEM, O_RDWR | O_SYNC)) < 0) {
		printf("open of " DEV_MEM " failed");
		return 1;
	} else 
		if (verbose) printf("open(" DEV_MEM "O_RDWR | O_SYNC) okay\n");

	mask_addr = (ADC_CR & ~MAP_MASK);  // preparation of mask_addr (base of the memory accessed)

	if (verbose) printf ("Mask address = %08x\n",mask_addr);
	
	mmap_ptr = (void *)-1;
	mmap_ptr = mmap(0, MAP_SIZE, PROT_READ | PROT_WRITE,MAP_SHARED, mem_fd, mask_addr);
	if (verbose) printf ("Mmap_ptr = %08x\n",mmap_ptr);

					   
	if ((void *)-1 == mmap_ptr) {
		printf("addr=0x%x, mask_addr=0x%lx :\n", ADC_CR, mask_addr);
		printf("    mmap");
		return 1; 
	} 
	if (verbose) printf("mmap() ok, mask_addr=0x%lx, mmap_ptr=%p\n", mask_addr, mmap_ptr);
	mapped_ADC_CR = mmap_ptr + ( ADC_CR & MAP_MASK);
	mapped_ADC_MR = mmap_ptr + ( ADC_MR & MAP_MASK); 
	mapped_ADC_SEQR1 = mmap_ptr + ( ADC_SEQR1 & MAP_MASK);
	mapped_ADC_SEQR2 = mmap_ptr + ( ADC_SEQR2 & MAP_MASK);
	mapped_ADC_CHER = mmap_ptr + ( ADC_CHER & MAP_MASK);
	mapped_ADC_CHDR = mmap_ptr + ( ADC_CHDR & MAP_MASK);
	mapped_ADC_CHSR = mmap_ptr + ( ADC_CHSR & MAP_MASK);
	mapped_ADC_LCDR = mmap_ptr + ( ADC_LCDR & MAP_MASK);
	mapped_ADC_IER = mmap_ptr + ( ADC_IER & MAP_MASK);
	mapped_ADC_IDR = mmap_ptr + ( ADC_IDR & MAP_MASK);
	mapped_ADC_IMR = mmap_ptr + ( ADC_IMR & MAP_MASK);
	mapped_ADC_ISR = mmap_ptr + ( ADC_ISR & MAP_MASK);
	mapped_ADC_OVER = mmap_ptr + ( ADC_OVER & MAP_MASK);
	mapped_ADC_EMR = mmap_ptr + ( ADC_EMR & MAP_MASK);
	mapped_ADC_CWR = mmap_ptr + ( ADC_CWR & MAP_MASK);
	mapped_ADC_CDR0 = mmap_ptr + ( ADC_CDR0 & MAP_MASK);
	mapped_ADC_CDR1 = mmap_ptr + ( ADC_CDR1 & MAP_MASK);
	mapped_ADC_CDR2 = mmap_ptr + ( ADC_CDR2 & MAP_MASK);
	mapped_ADC_CDR3 = mmap_ptr + ( ADC_CDR3 & MAP_MASK);
	mapped_ADC_CDR4 = mmap_ptr + ( ADC_CDR4 & MAP_MASK);
	mapped_ADC_CDR5 = mmap_ptr + ( ADC_CDR5 & MAP_MASK);
	mapped_ADC_CDR6 = mmap_ptr + ( ADC_CDR6 & MAP_MASK);
	mapped_ADC_CDR7 = mmap_ptr + ( ADC_CDR7 & MAP_MASK);
	mapped_ADC_CDR8 = mmap_ptr + ( ADC_CDR8 & MAP_MASK);
	mapped_ADC_CDR9 = mmap_ptr + ( ADC_CDR9 & MAP_MASK);
	mapped_ADC_CDR10 = mmap_ptr + ( ADC_CDR10 & MAP_MASK);
	mapped_ADC_CDR11 = mmap_ptr + ( ADC_CDR11 & MAP_MASK);
	mapped_ADC_TRGR = mmap_ptr + ( ADC_TRGR & MAP_MASK);

        // to map in a local page 2 the peripheral address registers used
        mask_addr2 = (PMC_PCER & ~MAP_MASK);  // preparation of mask_addr (base of the memory accessed)

        if (verbose) printf ("Mask address = %08x\n",mask_addr2);

        mmap_ptr2 = (void *)-1;
        mmap_ptr2 = mmap(0, MAP_SIZE, PROT_READ | PROT_WRITE,MAP_SHARED, mem_fd, mask_addr2);
        if (verbose) printf ("Mmap_ptr2 = %08x\n",mmap_ptr2);


        if ((void *)-1 == mmap_ptr2) {
                printf("addr=0x%x, mask_addr=0x%lx :\n", PMC_PCER, mask_addr);
                printf("    mmap");
                return 1;
        }

	if (verbose) printf("mmap() ok, mask_addr2=0x%lx, mmap_ptr2=%p\n", mask_addr2, mmap_ptr2);
	mapped_PMC_PCER = mmap_ptr2 + ( PMC_PCER & MAP_MASK);

	return 0;
}

close_memoryToIO(void) {
	// closing memory mapping
	if (-1 == munmap(mmap_ptr, MAP_SIZE)) {
		printf("mmap_ptr=%p:\n", mmap_ptr);
		printf("    munmap");
		return 1;
	} else if (verbose) printf("call of munmap() ok, mmap_ptr=%p\n", mmap_ptr);

        if (-1 == munmap(mmap_ptr2, MAP_SIZE)) {
                printf("mmap_ptr2=%p:\n", mmap_ptr2);
                printf("    munmap");
                return 1;
        } else if (verbose) printf("call of munmap() ok, mmap_ptr2=%p\n", mmap_ptr2);

	if (mem_fd >= 0)
		close(mem_fd);
	return 0;
}

adc_reset(void) {
	// SW & HW Reset of ADC section of AT91SAMG25
	*((unsigned long *) mapped_ADC_CR) = 0x00000001;
	return 0;
}

adc_enable(void) {
	// Enable PID 19 (ADC) in PMC_PCER Register (clock)
	*((unsigned long *) mapped_PMC_PCER) = 0x00080000;
	return 0;
}

adc_sample_freq_setup(void) {
	// setup ADC_MR using following values
	// USEQ=0x0
	// TRACKTIM = 0x8
	// STARTUP = 0x8
	// PRESCAL = 0x20
	// FWUP = 0, SLEEP =0, LOWRES = 0
	*((unsigned long *) mapped_ADC_MR) = 0x08082000;
}

adc_ch_select( unsigned long ch) {
        // run sampling on channel ch
        // after this we need to wait for sample ready with adc_sample_rdy

        *((unsigned long *) mapped_ADC_CHDR) = 0x00000fff;
        *((unsigned long *) mapped_ADC_CHER) = ch;
        if (verbose) printf("ADC_CHDR = 0x00000fff, ADC_CHER =%08x\n",  ch);

        return 0;
}

adc_run () {
	// run sampling on channel ch 
	// after this we need to wait for sample ready with adc_sample_rdy
	
	*((unsigned long *) mapped_ADC_CR) = 0x00000002;

	return 0;
}

adc_wait (void) {
	// waits for data ready in ADC_LCDR
	// data is ready when DRDY == 1
	while ( (*((unsigned long *) mapped_ADC_ISR) & 0x01000000) == 0) {

	}
	return 0;
}

unsigned long adc_read (int ch) {
	// reads converted sample from LCDR
	unsigned long Sample;
	
	Sample = *((unsigned long *) mapped_ADC_LCDR) & 0x00000fff;
 	switch ( ch )
	{
		case 0:
			Sample = *((unsigned long *) mapped_ADC_CDR0);
			break;

		case 1:
			Sample = *((unsigned long *) mapped_ADC_CDR1);
            break;

        case 2: 
            Sample = *((unsigned long *) mapped_ADC_CDR2);
            break;

        case 3:
            Sample = *((unsigned long *) mapped_ADC_CDR3);
            break;
	}
	return Sample;
}

int main(int argc, char * argv[])
{
	unsigned long Sample0;
	unsigned long Sample1;
	unsigned long Sample2;
	unsigned long Sample3;
	int i;
	
	printf("ADC direct access example program for AriaG25\n");
	
	if (init_memoryToIO()) {
		printf ("Error in init_memoryToIO() \n");
		return 1;
	}		

	adc_reset();
	adc_enable();
	adc_sample_freq_setup();

	adc_ch_select (1|2|4|8);

	//waits for tracking completion
	sleep (1);
	printf("Start!\n");

	for (i=0; i<1000000; i++)
	{
		adc_run();
		adc_wait();

/*		
		Sample = adc_read(0);
		printf("ADC read LDATA CH0:  %08x\n",Sample);

		Sample = adc_read(1);
		printf("ADC read LDATA CH1:  %08x\n",Sample);

		Sample = adc_read(2);
        	printf("ADC read LDATA CH2:  %08x\n",Sample);

		Sample = adc_read(3);
		printf("ADC read LDATA CH3:  %08x\n\n",Sample);
*/
		Sample0 = adc_read(0);
		Sample1 = adc_read(1);
		Sample2 = adc_read(2);
		Sample3 = adc_read(3);
		printf("ADC read CH0: 0x%03x; CH1: 0x%03x; CH2: 0x%03x; CH3: 0x%03x \n",Sample0,Sample1,Sample2,Sample3);
		sleep(1);
	} 

	
	if (close_memoryToIO()) {
		printf ("Error in close_memoryToIO() \n");
		return 1;
	}		
 		
}

