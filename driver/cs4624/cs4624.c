#include "cs4624.h"
#include "mixer.h"
#include "cs4624_image.h"
#include "register.h"

#define CS4624_DEBUG
#ifdef CS4624_DEBUG
#define FUNC_LOG()  printf(KERN_EMERG "FUNC_LOG: [%d][%s()]\n", __LINE__, __FUNCTION__)
#endif
// type declared in libaudio
/* global value */
DEV_STRUCT dev;
aud_sub_dev_conf_t aud_conf[3];
sub_dev_t sub_dev[3];
special_file_t special_file[3];
drv_t drv;

#define GOF_PER_SEC 200

/* internal function */
static int dev_probe(void);
static int set_sample_rate(u32_t rate, int num);
static int set_stereo(u32_t stereo, int num);
static int set_bits(u32_t bits, int sub_dev);
static int set_frag_size(u32_t frag_size, int num);
static int set_sign(u32_t val, int num);
static int get_frag_size(u32_t *val, int *len, int num);
static int free_buf(u32_t *val, int *len, int num);

/* developer interface */
static int dev_reset(u32_t *base);
static void dev_configure(u32_t *base);
static void dev_init_mixer(DEV_STRUCT *dev);
static void dev_set_sample_rate(u32_t *base, u16_t sample_rate);
static void dev_set_format(u32_t *base, u32_t bits, u32_t sign,
							u32_t stereo, u32_t sample_count);
static void dev_start_channel(u32_t *base, int sub_dev);
static void dev_stop_channel(u32_t *base, int sub_dev);
static void dev_set_dma(u32_t *base, u32_t dma, u32_t len, int sub_dev);
static u32_t dev_read_dma_current(u32_t *base, int sub_dev);
static void dev_pause_dma(DEV_STRUCT *dev, int sub_dev);
static void dev_resume_dma(DEV_STRUCT *dev, int sub_dev);
static void dev_intr_other(u32_t *base, u32_t status);
static u32_t dev_read_clear_intr_status(DEV_STRUCT *dev);
static void dev_intr_enable(DEV_STRUCT *dev, int flag);

/* ======= Developer implemented function ======= */
/* ====== Self-defined function ====== */

/* ====== Mixer handling interface ======*/
/* Write the data to mixer register (### WRITE_MIXER_REG ###) */

// snd_mychip_codec_write
void dev_mixer_write(DEV_STRUCT* dev, u32_t reg, u32_t val) {
	u32_t i, data;

	/*
	 *  1. Write ACCAD = Command Address Register = 46Ch for AC97 register address
	 *  2. Write ACCDA = Command Data Register = 470h    for data to write to AC97 
	 *  3. Write ACCTL = Control Register = 460h for initiating the write7---55
	 *  4. Read ACCTL = 460h, DCV should be reset by now and 460h = 17h
	 *  5. if DCV not cleared, break and return error
	 *  6. Read ACSTS = Status Register = 464h, check VSTS bit
	 */
	/*
	 *  Setup the AC97 control registers on the CS461x to send the
	 *  appropriate command to the AC97 to perform the read.
	 *  ACCAD = Command Address Register = 46Ch
	 *  ACCDA = Command Data Register = 470h
	 *  ACCTL = Control Register = 460h
	 *  set DCV - will clear when process completed
	 *  reset CRW - Write command
	 *  set VFRM - valid frame enabled
	 *  set ESYN - ASYNC generation enabled
	 *  set RSTN - ARST# inactive, AC97 codec not reset
     */

	//sdr_out32(base0, REG_CODEC_ADDR, reg);
	//sdr_out32(base0, REG_CODEC_DATA, val);
	//sdr_out32(base0, REG_CODEC_CTRL, 0x0e);

	snd_mychip_pokeBA0(dev, BA0_ACCAD , reg);
	snd_mychip_pokeBA0(dev, BA0_ACCDA , val);
	snd_mychip_peekBA0(dev, BA0_ACCTL);

	snd_mychip_pokeBA0(dev, BA0_ACCTL, /* clear ACCTL_DCV */ ACCTL_VFRM |
			   ACCTL_ESYN | ACCTL_RSTN);
	snd_mychip_pokeBA0(dev, BA0_ACCTL, ACCTL_DCV | ACCTL_VFRM |
			   ACCTL_ESYN | ACCTL_RSTN);

	for (i = 0; i < 50000; i++) {
		/*
		 *  First, we want to wait for a short time.
		 */
		micro_delay(10);
		/*
		 *  Now, check to see if the write has completed.
		 *  ACCTL = 460h, DCV should be reset by now and 460h = 07h
		 */
		//if codec_done
		if (!(snd_mychip_peekBA0(dev, BA0_ACCTL) & ACCTL_DCV)) {
			goto end;
		}
	}
	printf("SDR: Codec is not ready in write\n");
	printf("AC'97 write problem, reg = 0x%x, val = 0x%x\n", reg, val);

end:
	return;
}

/* Read the data from mixer register (### READ_MIXER_REG ###) */
// snd_mychip_codec_read
u32_t dev_mixer_read(DEV_STRUCT* dev, u32_t reg) {
	u32_t i, data, tmp, result, count;
	/*
	 *  1. Write ACCAD = Command Address Register = 46Ch for AC97 register address
	 *  2. Write ACCDA = Command Data Register = 470h    for data to write to AC97 
	 *  3. Write ACCTL = Control Register = 460h for initiating the write7---55
	 *  4. Read ACCTL = 460h, DCV should be reset by now and 460h = 17h
	 *  5. if DCV not cleared, break and return error
	 *  6. Read ACSTS = Status Register = 464h, check VSTS bit
	 */

	snd_mychip_peekBA0(dev, BA0_ACSDA);

	tmp = snd_mychip_peekBA0(dev, BA0_ACCTL);
	if ((tmp & ACCTL_VFRM) == 0) {
		printf("mychip: ACCTL_VFRM not set 0x%x\n",tmp);
		snd_mychip_pokeBA0(dev, BA0_ACCTL, (tmp & (~ACCTL_ESYN)) | ACCTL_VFRM );
		micro_delay(50);
		tmp = snd_mychip_peekBA0(dev, BA0_ACCTL);
		snd_mychip_pokeBA0(dev, BA0_ACCTL, tmp | ACCTL_ESYN | ACCTL_VFRM );

	}

	/*
	 *  Setup the AC97 control registers on the CS461x to send the
	 *  appropriate command to the AC97 to perform the read.
	 *  ACCAD = Command Address Register = 46Ch
	 *  ACCDA = Command Data Register = 470h
	 *  ACCTL = Control Register = 460h
	 *  set DCV - will clear when process completed
	 *  set CRW - Read command
	 *  set VFRM - valid frame enabled
	 *  set ESYN - ASYNC generation enabled
	 *  set RSTN - ARST# inactive, AC97 codec not reset
	 */

	snd_mychip_pokeBA0(dev, BA0_ACCAD, reg);
	snd_mychip_pokeBA0(dev, BA0_ACCDA, 0);


	snd_mychip_pokeBA0(dev, BA0_ACCTL,/* clear ACCTL_DCV */ ACCTL_CRW | 
			ACCTL_VFRM | ACCTL_ESYN |
			ACCTL_RSTN);
	snd_mychip_pokeBA0(dev, BA0_ACCTL, ACCTL_DCV | ACCTL_CRW |
			ACCTL_VFRM | ACCTL_ESYN |
			ACCTL_RSTN);
	/*
	 *  Wait for the read to occur.
	 */
	for (count = 0; count < 1000; count++) {
		/*
		 *  First, we want to wait for a short time.
		 */
		micro_delay(10);
		/*
		 *  Now, check to see if the read has completed.
		 *  ACCTL = 460h, DCV should be reset by now and 460h = 17h
		 */
		if (!(snd_mychip_peekBA0(dev, BA0_ACCTL) & ACCTL_DCV))
			goto ok1;
	}
	printf("AC'97 read problem (ACCTL_DCV), reg = 0x%x\n", reg);
	result = 0xffff;
	goto end;

ok1:
	/*
	 *  Wait for the valid status bit to go active.
	 */
	for (count = 0; count < 100; count++) {
		/*
		 *  Read the AC97 status register.
		 *  ACSTS = Status Register = 464h
		 *  VSTS - Valid Status
		 */
		if (snd_mychip_peekBA0(dev, BA0_ACSTS) & ACSTS_VSTS)
			goto ok2;
		micro_delay(10);
	}

	printf("AC'97 read problem (ACSTS_VSTS), reg = 0x%x\n", reg);
	result = 0xffff;
	goto end;

ok2:
	/*
	 *  Read the data returned from the AC97 register.
	 *  ACSDA = Status Data Register = 474h
	 */
	result = snd_mychip_peekBA0(dev, BA0_ACSDA);
end:
	return result;
}

/* ====== Developer interface ======*/

/* Reset the device (### RESET_HARDWARE_CAN_FAIL ###)
 * -- Return OK means success, Others means failure */
static int dev_init(DEV_STRUCT* dev) {

	int timeout;
	/* 
	 *  First, blast the clock control register to zero so that the PLL starts
	 *  out in a known state, and blast the master serial port control register
	 *  to zero so that the serial ports also start out in a known state.
	 */
	snd_mychip_pokeBA0(dev, BA0_CLKCR1, 0);
	snd_mychip_pokeBA0(dev, BA0_SERMC1, 0);

	/*
	 *  If we are in AC97 mode, then we must set the part to a host controlled
	 *  AC-link.  Otherwise, we won't be able to bring up the link.
	 */        

	snd_mychip_pokeBA0(dev, BA0_SERACC, SERACC_HSP | SERACC_CHIP_TYPE_1_03); /* 1.03 codec */


	/*
	 *  Drive the ARST# pin low for a minimum of 1uS (as defined in the AC97
	 *  spec) and then drive it high.  This is done for non AC97 modes since
	 *  there might be logic external to the CS461x that uses the ARST# line
	 *  for a reset.
	 */
	snd_mychip_pokeBA0(dev, BA0_ACCTL, 0);

	micro_delay(50);
	snd_mychip_pokeBA0(dev, BA0_ACCTL, ACCTL_RSTN);

	/*
	 *  The first thing we do here is to enable sync generation.  As soon
	 *  as we start receiving bit clock, we'll start producing the SYNC
	 *  signal.
	 */
	snd_mychip_pokeBA0(dev, BA0_ACCTL, ACCTL_ESYN | ACCTL_RSTN);

	/*
	 *  Now wait for a short while to allow the AC97 part to start
	 *  generating bit clock (so we don't try to start the PLL without an
	 *  input clock).
	 */
	micro_delay(10);

	/*
	 *  Set the serial port timing configuration, so that
	 *  the clock control circuit gets its clock from the correct place.
	 */
	snd_mychip_pokeBA0(dev, BA0_SERMC1, SERMC1_PTC_AC97);

	/*
	 *  Write the selected clock control setup to the hardware.  Do not turn on
	 *  SWCE yet (if requested), so that the devices clocked by the output of
	 *  PLL are not clocked until the PLL is stable.
	 */
	snd_mychip_pokeBA0(dev, BA0_PLLCC, PLLCC_LPF_1050_2780_KHZ | PLLCC_CDR_73_104_MHZ);
	snd_mychip_pokeBA0(dev, BA0_PLLM, 0x3a);
	snd_mychip_pokeBA0(dev, BA0_CLKCR2, CLKCR2_PDIVS_8);

	/*
	 *  Power up the PLL.
	 */
	snd_mychip_pokeBA0(dev, BA0_CLKCR1, CLKCR1_PLLP);

	/*
	 *  Wait until the PLL has stabilized.
	 */
	micro_delay(100);

	/*
	 *  Turn on clocking of the core so that we can setup the serial ports.
	 */
	snd_mychip_pokeBA0(dev, BA0_CLKCR1, CLKCR1_PLLP | CLKCR1_SWCE);

	/*
	 * Enable FIFO  Host Bypass
	 */
	snd_mychip_pokeBA0(dev, BA0_SERBCF, SERBCF_HBP);

	/*
	 *  Fill the serial port FIFOs with silence.
	 */
	//snd_mychip_clear_serial_FIFOs(&dev);

	/*
	 *  Set the serial port FIFO pointer to the first sample in the FIFO.
	 */
	/* snd_mychip_pokeBA0(&dev, BA0_SERBSP, 0); */

	/*
	 *  Write the serial port configuration to the part.  The master
	 *  enable bit is not set until all other values have been written.
	 */
	snd_mychip_pokeBA0(dev, BA0_SERC1, SERC1_SO1F_AC97 | SERC1_SO1EN);
	snd_mychip_pokeBA0(dev, BA0_SERC2, SERC2_SI1F_AC97 | SERC1_SO1EN);
	snd_mychip_pokeBA0(dev, BA0_SERMC1, SERMC1_PTC_AC97 | SERMC1_MSPE);

	micro_delay(5);


	/*
	 * Wait for the codec ready signal from the AC97 codec.
	 */
	timeout = 150;
	while (timeout-- > 0) {
		/*
		 *  Read the AC97 status register to see if we've seen a CODEC READY
		 *  signal from the AC97 codec.
		 */
		if (snd_mychip_peekBA0(dev, BA0_ACSTS) & ACSTS_CRDY)
			goto ok1;
		micro_delay(10);
	}


	printf("create - never read codec ready from AC'97\n");
	printf("it is not probably bug, try to use CS4236 driver\n");
	return -EIO;
ok1:

	/*
	 *  Assert the vaid frame signal so that we can start sending commands
	 *  to the AC97 codec.
	 */
	snd_mychip_pokeBA0(dev, BA0_ACCTL, ACCTL_VFRM | ACCTL_ESYN | ACCTL_RSTN);


	/*
	 *  Wait until we've sampled input slots 3 and 4 as valid, meaning that
	 *  the codec is pumping ADC data across the AC-link.
	 */
	timeout = 150;
	while (timeout-- > 0) {
		/*
		 *  Read the input slot valid register and see if input slots 3 and
		 *  4 are valid yet.
		 */
		if ((snd_mychip_peekBA0(dev, BA0_ACISV) & (ACISV_ISV3 | ACISV_ISV4)) == (ACISV_ISV3 | ACISV_ISV4))
			goto ok2;
		msleep(10);
	}

	/* This may happen on a cold boot with a Terratec SiXPack 5.1.
	   Reloading the driver may help, if there's other soundcards 
	   with the same problem I would like to know. (Benny) */

	printf("ERROR: snd-mychip: never read ISV3 & ISV4 from AC'97\n");
	printf("       Try reloading the ALSA driver, if you find something\n");
	printf("       broken or not working on your soundcard upon\n");
	printf("       this message please report to alsa-devel@alsa-project.org\n");

	return -EIO;

ok2:

	/*
	 *  Now, assert valid frame and the slot 3 and 4 valid bits.  This will
	 *  commense the transfer of digital audio data to the AC97 codec.
	 */

	snd_mychip_pokeBA0(dev, BA0_ACOSV, ACOSV_SLV3 | ACOSV_SLV4);


	/*
	 *  Power down the DAC and ADC.  We will power them up (if) when we need
	 *  them.
	 */
	/* snd_mychip_pokeBA0(&dev, BA0_AC97_POWERDOWN, 0x300); */

	/*
	 *  Turn off the Processor by turning off the software clock enable flag in 
	 *  the clock control register.
	 */
	/* tmp = snd_mychip_peekBA0(&dev, BA0_CLKCR1) & ~CLKCR1_SWCE; */
	/* snd_mychip_pokeBA0(&dev, BA0_CLKCR1, tmp); */

	return OK;
}

int snd_mychip_download(DEV_STRUCT *dev,
		u32_t *src,
		unsigned long offset,
		unsigned long len)
{
	u32_t dst;
	unsigned int bank = offset >> 16;
	offset = offset & 0xffff;

	dst = dev->ba1_region.idx[bank].remap_addr + offset;
	len /= sizeof(u32_t);

	/* writel already converts 32-bit value to right endianess */
	while (len-- > 0) {
		sdr_out32(dst, offset, *src++);
		dst += sizeof(u32_t);
	}
	return 0;
}

int snd_mychip_download_image(DEV_STRUCT *dev)
{
	int idx, err;
	unsigned long offset = 0;

	for (idx = 0; idx < BA1_MEMORY_COUNT; idx++) {
		if ((err = snd_mychip_download(dev,
						&BA1Struct.map[offset],
						BA1Struct.memory[idx].offset,
						BA1Struct.memory[idx].size)) < 0)
			return err;
		offset += BA1Struct.memory[idx].size >> 2;
	} 
	return 0;
}

static void snd_mychip_reset(DEV_STRUCT *dev){
	int idx;

	/*
	 *  Write the reset bit of the SP control register.
	 */
	snd_mychip_pokeBA1(dev, BA1_SPCR, SPCR_RSTSP);

	/*
	 *  Write the control register.
	 */
	snd_mychip_pokeBA1(dev, BA1_SPCR, SPCR_DRQEN);

	/*
	 *  Clear the trap registers.
	 */
	for (idx = 0; idx < 8; idx++) {
		snd_mychip_pokeBA1(dev, BA1_DREG, DREG_REGID_TRAP_SELECT + idx);
		snd_mychip_pokeBA1(dev, BA1_TWPR, 0xFFFF);
	}
	snd_mychip_pokeBA1(dev, BA1_DREG, 0);

	/*
	 *  Set the frame timer to reflect the number of cycles per frame.
	 */
	snd_mychip_pokeBA1(dev, BA1_FRMT, 0xadf);
}

/* Configure hardware registers (### CONF_HARDWARE ###) */
static void dev_configure(u32_t *base) {
	u32_t i, data, base0 = base[0];
	sdr_out32(base0, REG_MASTER_CTRL, CMD_PORT_TIMING | CMD_AC97_MODE |
										CMD_MASTER_SERIAL);
	sdr_out32(base0, REG_CLK_CTRL, 0x10);
	micro_delay(50);
	sdr_out32(base0, REG_CLK_CTRL, 0x30);
	micro_delay(500);
	sdr_out32(base0, REG_CODEC_CTRL, 0x02);
	micro_delay(500);
	sdr_out32(base0, REG_CODEC_CTRL, 0x06);
	micro_delay(500);
	sdr_out32(base0, REG_CODEC_OSV, 0x03);
	sdr_out32(base0, REG_PCM_LVOL, 0x07);
	sdr_out32(base0, REG_PCM_RVOL, 0x07);
}

/* Initialize the mixer (### INIT_MIXER ###) */
static void dev_init_mixer(DEV_STRUCT *dev) {
	dev_mixer_write(dev, 0, 0);
}

/* Set DAC and ADC sample rate (### SET_SAMPLE_RATE ###) */
static void dev_set_playback_sample_rate(DEV_STRUCT *dev, u16_t rate) {
	unsigned long flags;
	unsigned int tmp1, tmp2;
	unsigned int phiIncr;
	unsigned int correctionPerGOF, correctionPerSec;

	/*
	 *  Compute the values used to drive the actual sample rate conversion.
	 *  The following formulas are being computed, using inline assembly
	 *  since we need to use 64 bit arithmetic to compute the values:
	 *
	 *  phiIncr = floor((Fs,in * 2^26) / Fs,out)
	 *  correctionPerGOF = floor((Fs,in * 2^26 - Fs,out * phiIncr) /
	 *                                   GOF_PER_SEC)
	 *  ulCorrectionPerSec = Fs,in * 2^26 - Fs,out * phiIncr -M
	 *                       GOF_PER_SEC * correctionPerGOF
	 *
	 *  i.e.
	 *
	 *  phiIncr:other = dividend:remainder((Fs,in * 2^26) / Fs,out)
	 *  correctionPerGOF:correctionPerSec =
	 *      dividend:remainder(ulOther / GOF_PER_SEC)
	 */
	tmp1 = rate << 16;
	phiIncr = tmp1 / 48000;
	tmp1 -= phiIncr * 48000;
	tmp1 <<= 10;
	phiIncr <<= 10;
	tmp2 = tmp1 / 48000;
	phiIncr += tmp2;
	tmp1 -= tmp2 * 48000;
	correctionPerGOF = tmp1 / GOF_PER_SEC;
	tmp1 -= correctionPerGOF * GOF_PER_SEC;
	correctionPerSec = tmp1;

	/*
	 *  Fill in the SampleRateConverter control block.
	 */

	snd_mychip_pokeBA1(dev, BA1_PSRC,
			((correctionPerSec << 16) & 0xFFFF0000) | (correctionPerGOF & 0xFFFF));
	snd_mychip_pokeBA1(dev, BA1_PPI, phiIncr);


	// u32_t i, data = 0, base0 = base[0];
	// for (i = 0; i < 6; i++) {
	// 	if (g_sample_rate[i] == sample_rate) {
	// 		data = i;
	// 		break;
	// 	}
	// }
	// sdr_out32(base0, REG_DAC_SAMPLE_RATE, data);
	// sdr_out32(base0, REG_ADC_SAMPLE_RATE, data);
}
static void dev_set_capture_sample_rate(DEV_STRUCT *dev, unsigned int rate)
{
	unsigned long flags;
	unsigned int phiIncr, coeffIncr, tmp1, tmp2;
	unsigned int correctionPerGOF, correctionPerSec, initialDelay;
	unsigned int frameGroupLength, cnt;

	/*
	 *  We can only decimate by up to a factor of 1/9th the hardware rate.
	 *  Correct the value if an attempt is made to stray outside that limit.
	 */
	if ((rate * 9) < 48000)
		rate = 48000 / 9;

	/*
	 *  We can not capture at at rate greater than the Input Rate (48000).
	 *  Return an error if an attempt is made to stray outside that limit.
	 */
	if (rate > 48000)
		rate = 48000;

	/*
	 *  Compute the values used to drive the actual sample rate conversion.
	 *  The following formulas are being computed, using inline assembly
	 *  since we need to use 64 bit arithmetic to compute the values:
	 *
	 *     coeffIncr = -floor((Fs,out * 2^23) / Fs,in)
	 *     phiIncr = floor((Fs,in * 2^26) / Fs,out)
	 *     correctionPerGOF = floor((Fs,in * 2^26 - Fs,out * phiIncr) /
	 *                                GOF_PER_SEC)
	 *     correctionPerSec = Fs,in * 2^26 - Fs,out * phiIncr -
	 *                          GOF_PER_SEC * correctionPerGOF
	 *     initialDelay = ceil((24 * Fs,in) / Fs,out)
	 *
	 * i.e.
	 *
	 *     coeffIncr = neg(dividend((Fs,out * 2^23) / Fs,in))
	 *     phiIncr:ulOther = dividend:remainder((Fs,in * 2^26) / Fs,out)
	 *     correctionPerGOF:correctionPerSec =
	 *       dividend:remainder(ulOther / GOF_PER_SEC)
	 *     initialDelay = dividend(((24 * Fs,in) + Fs,out - 1) / Fs,out)
	 */

	tmp1 = rate << 16;
	coeffIncr = tmp1 / 48000;
	tmp1 -= coeffIncr * 48000;
	tmp1 <<= 7;
	coeffIncr <<= 7;
	coeffIncr += tmp1 / 48000;
	coeffIncr ^= 0xFFFFFFFF;
	coeffIncr++;
	tmp1 = 48000 << 16;
	phiIncr = tmp1 / rate;
	tmp1 -= phiIncr * rate;
	tmp1 <<= 10;
	phiIncr <<= 10;
	tmp2 = tmp1 / rate;
	phiIncr += tmp2;
	tmp1 -= tmp2 * rate;
	correctionPerGOF = tmp1 / GOF_PER_SEC;
	tmp1 -= correctionPerGOF * GOF_PER_SEC;
	correctionPerSec = tmp1;
	initialDelay = ((48000 * 24) + rate - 1) / rate;

	/*
	 *  Fill in the VariDecimate control block.
	 */
	snd_mychip_pokeBA1(dev, BA1_CSRC,
			((correctionPerSec << 16) & 0xFFFF0000) | (correctionPerGOF & 0xFFFF));
	snd_mychip_pokeBA1(dev, BA1_CCI, coeffIncr);
	snd_mychip_pokeBA1(dev, BA1_CD,
			(((BA1_VARIDEC_BUF_1 + (initialDelay << 2)) << 16) & 0xFFFF0000) | 0x80);
	snd_mychip_pokeBA1(dev, BA1_CPI, phiIncr);

	/*
	 *  Figure out the frame group length for the write back task.  Basically,
	 *  this is just the factors of 24000 (2^6*3*5^3) that are not present in
	 *  the output sample rate.
	 */
	frameGroupLength = 1;
	for (cnt = 2; cnt <= 64; cnt *= 2) {
		if (((rate / cnt) * cnt) != rate)
			frameGroupLength *= 2;
	}
	if (((rate / 3) * 3) != rate) {
		frameGroupLength *= 3;
	}
	for (cnt = 5; cnt <= 125; cnt *= 5) {
		if (((rate / cnt) * cnt) != rate) 
			frameGroupLength *= 5;
	}

	/*
	 * Fill in the WriteBack control block.
	 */
	snd_mychip_pokeBA1(dev, BA1_CFG1, frameGroupLength);
	snd_mychip_pokeBA1(dev, BA1_CFG2, (0x00800000 | frameGroupLength));
	snd_mychip_pokeBA1(dev, BA1_CCST, 0x0000FFFF);
	snd_mychip_pokeBA1(dev, BA1_CSPB, ((65536 * rate) / 24000));
	snd_mychip_pokeBA1(dev, (BA1_CSPB + 4), 0x0000FFFF);
}
/* Set DAC and ADC format (### SET_FORMAT ###)*/
static void dev_set_format(u32_t *base, u32_t bits, u32_t sign,
							u32_t stereo, u32_t sample_count) {
	u32_t base0 = base[0];
	dmr_data = CMD_DMR_INIT;
	if (stereo == 0)
		dmr_data |= CMD_DMR_MONO;
	if (sign == 0)
		dmr_data |= CMD_DMR_UNSIGN;
	if (bits == 8) {
		dmr_data |= CMD_DMR_BIT8;
		if (stereo == 0)
			dmr_data |= CMD_DMR_SWAP;
	}
	else if (bits == 32)
		dmr_data |= CMD_DMR_BIT32;
}

/* Start the channel (### START_CHANNEL ###) */
// in snd_cs4281_capture_prepare/snd_cs4281_playback_prepare
static void dev_start_channel(u32_t *base, int sub_dev) {
	u32_t temp, base0 = base[0];

	dcr_data = 0x30001;
	if (sub_dev == DAC)
		fcr_data = CMD_DAC_FCR_INIT;
	if (sub_dev == ADC)
		fcr_data = CMD_ADC_FCR_INIT;
	dmr_data |= CMD_DMR_DMA;
	dcr_data &= ~CMD_DCR_MASK;
	fcr_data |= CMD_FCR_FEN;
	if (sub_dev == DAC) {
		dmr_data |= CMD_DMR_WRITE;
		sdr_out32(base0, REG_DAC_FSIC, 0);
		sdr_out32(base0, REG_DAC_DMR, dmr_data & ~CMD_DMR_DMA);
		sdr_out32(base0, REG_DAC_DMR, dmr_data);
		sdr_out32(base0, REG_DAC_FCR, fcr_data);
		sdr_out32(base0, REG_DAC_DCR, dcr_data);
	}
	else if (sub_dev == ADC) {
		dmr_data |= CMD_DMR_READ;
		sdr_out32(base0, REG_ADC_FSIC, 0);
		sdr_out32(base0, REG_ADC_DMR, dmr_data & ~CMD_DMR_DMA);
		sdr_out32(base0, REG_ADC_DMR, dmr_data);
		sdr_out32(base0, REG_ADC_FCR, fcr_data);
		sdr_out32(base0, REG_ADC_DCR, dcr_data);
	}




}

/* Stop the channel (### STOP_CHANNEL ###) */
static void dev_stop_channel(u32_t *base, int sub_dev) {
	u32_t base0 = base[0];
	dmr_data &= ~(CMD_DMR_DMA | CMD_DMR_POLL);
	dcr_data |= ~CMD_DCR_MASK;
	fcr_data &= ~CMD_FCR_FEN;
	if (sub_dev == DAC) {
		sdr_out32(base0, REG_DAC_DMR, dmr_data);
		sdr_out32(base0, REG_DAC_FCR, fcr_data);
		sdr_out32(base0, REG_DAC_DCR, dcr_data);
	}
	else if (sub_dev == ADC) {
		sdr_out32(base0, REG_ADC_DMR, dmr_data);
		sdr_out32(base0, REG_ADC_FCR, fcr_data);
		sdr_out32(base0, REG_ADC_DCR, dcr_data);
	}
}

/* Set DMA address and length (### SET_DMA ###) */
static void dev_set_dma(u32_t *base, u32_t dma, u32_t len, int sub_dev) {
	u32_t base0 = base[0];

	if (sub_dev == DAC) {
		sdr_out32(base0, REG_DAC_DMA_ADDR, dma);
		sdr_out32(base0, REG_DAC_DMA_LEN, len - 1);
	}
	else if (sub_dev == ADC) {
		sdr_out32(base0, REG_ADC_DMA_ADDR, dma);
		sdr_out32(base0, REG_ADC_DMA_LEN, len - 1);
	}
}

/* Read current address (### READ_DMA_CURRENT_ADDR ###) */
static u32_t dev_read_dma_current(u32_t *base, int sub_dev) {
	u32_t data, base0 = base[0];
	if (sub_dev == DAC)
		data = sdr_in32(base0, REG_DAC_DCC);
	else if (sub_dev == ADC)
		data = sdr_in16(base0, REG_ADC_DCC);
	data &= 0xffff;
	return (u16_t)data;
}

/* Pause the DMA (### PAUSE_DMA ###) */
static void dev_pause_dma(DEV_STRUCT *dev, int sub_dev) {
	u32_t tmp;
	if (sub_dev == DAC) {
		tmp = snd_mychip_peekBA1(dev, BA1_PCTL);
		tmp &= 0x0000ffff;
		snd_mychip_pokeBA1(dev, BA1_PCTL, tmp);

	}
	if (sub_dev == ADC) {
		tmp = snd_mychip_peekBA1(dev, BA1_CCTL);
		tmp &= 0xffff0000;
		snd_mychip_pokeBA1(dev, BA1_CCTL, tmp);
	}
}

/* Resume the DMA (### RESUME_DMA ###) */
static void dev_resume_dma(DEV_STRUCT *dev, int sub_dev) {
	u32_t tmp;

	if (sub_dev == DAC) {
		tmp = snd_mychip_peekBA1(dev, BA1_PCTL);
		tmp &= 0x0000ffff;
		snd_mychip_pokeBA1(dev, BA1_PCTL, chip->play_ctl | tmp);
	}
	if (sub_dev == ADC) {
		tmp = snd_mychip_peekBA1(dev, BA1_CCTL);
		tmp &= 0xffff0000;
		snd_mychip_pokeBA1(dev, BA1_CCTL, chip->capt_ctl | tmp);

	}
}

/* Read and clear interrupt stats (### READ_CLEAR_INTR_STS ###)
 * -- Return interrupt status */
static u32_t dev_read_clear_intr_status(DEV_STRUCT *dev) {
	u32_t status, base0 = base[0];
	status = snd_mychip_peekBA0(dev, BA0_HISR);
	//sdr_in32(base0, REG_DAC_HDSR);
	//sdr_in32(base0, REG_ADC_HDSR);
	snd_mychip_pokeBA0(dev, BA0_HICR, HICR_CHGM | HICR_IEV);
	return status;
}

/* Enable or disable interrupt (### INTR_ENABLE_DISABLE ###) */
static void dev_intr_enable(DEV_STRUCT *dev, int flag) {
	u32_t data, base0 = base[0], tmp;
	if (flag == INTR_ENABLE) {
		snd_mychip_pokeBA0(chip, BA0_HICR, HICR_IEV | HICR_CHGM);
		tmp = snd_mychip_peekBA0(dev, BA1_PFIE);
		tmp &= ~0x0000f03f;
		snd_mychip_pokeBA1(dev, BA1_PFIE, tmp);	/* playback interrupt enable */

		tmp = snd_mychip_peekBA1(dev, BA1_CIE);
		tmp &= ~0x0000003f;
		tmp |=  0x00000001;
		snd_mychip_pokeBA1(dev, BA1_CIE, tmp);	/* capture interrupt enable */
	}
	else if (flag == INTR_DISABLE) {
		snd_mychip_pokeBA0(chip, BA0_HICR, HICR_IEV | HICR_CHGM);
		tmp = snd_mychip_peekBA1(dev, BA1_PFIE);
		tmp &= ~0x0000f03f;
		tmp |=  0x00000010;
		snd_mychip_pokeBA1(dev, BA1_PFIE, tmp);     /* playback interrupt disable */

		tmp = snd_mychip_peekBA1(dev, BA1_CIE);
		tmp &= ~0x0000003f;
		tmp |=  0x00000011;
		snd_mychip_pokeBA1(dev, BA1_CIE, tmp); /* capture interrupt disable */
	}
}

/* ======= Common driver function ======= */
/* Probe the device */
static int dev_probe(void) {
	int devind, i, ioflag;
	u32_t device, bar, size, base;
	u16_t vid, did, temp;
	u8_t *reg;
	/* dev_probe tries to find device and get IRQ and base address
	   with a little (much) help from the PCI library. 
	   This code is quite device independent and you can copy it. 
	   (just make sure to get the bugs out first)*/
	pci_init();
	device = pci_first_dev(devind, &vid, &did);
	while (&device > 0) {
		if (vid == VENDOR_ID && did == DEVICE_ID)
			break;
		device = pci_next_dev(devind, &vid, &did);
	}
	if (vid != VENDOR_ID || did != DEVICE_ID)
		return EIO;
	pci_reserve(devind);

	for (i = 0; i < 6; i++)
		dev.base[i] = 0;
#ifdef DMA_BASE_IOMAP
	for (i = 0; i < 6; i++) {
		if (pci_get_bar(devind, PCI_BAR + i * 4, &base, &size, &ioflag)) {
			/* printf("SDR: Fail to get PCI BAR %d\n", i); */
			continue;
		}
		if (ioflag) {
			/* printf("SDR: PCI BAR %d is not for memory\n", i); */
			continue;
		}
		dev.base[i] = (u32_t)base;
		if ((reg = vm_map_phys(SELF, (void *)base, size)) == MAP_FAILED) {
			printf("SDR: Fail to map hardware registers from PCI\n");
			return -EIO;
		}
		dev.remap_base[i] = (u32_t)reg;
	}
	FUNC_LOG();
#else
	/* Get PCI BAR0-5 */
		/* get base address of our device, ignore least signif. bit 
	   this last bit thing could be device dependent, i don't know ??? */
	for (i = 0; i < 6; i++){
		dev.base[i] = base = pci_attr_r32(&devind, PCI_BAR + i * 4) & 0xffffffe0;
		if ((reg = vm_map_phys(SELF, (void *)base, size)) == MAP_FAILED) {
			printf("SDR: Fail to map hardware registers from PCI\n");
			return -EIO;
		}
		dev.remap_base[i] = (u32_t)reg;
	}
	FUNC_LOG();
#endif
	dev.name = pci_dev_name(vid, did);
	dev.irq = pci_attr_r8(&devind, PCI_ILR);
	dev.revision = pci_attr_r8(&devind, PCI_REV);
	dev.did = did;
	dev.vid = vid;
	dev.devind = devind;
	//temp = pci_attr_r16(&devind, PCI_CR);
	//pci_attr_w16(&devind, PCI_CR, temp | 0x105);

#ifdef MY_DEBUG
	printf("SDR: Hardware name is %s\n", dev.name);
	for (i = 0; i < 6; i++)
		printf("SDR: PCI BAR%d is 0x%08x\n", i, dev.base[i]);
	printf("SDR: IRQ number is 0x%02x\n", dev.irq);
#endif
	return OK;
}


/* Set sample rate in configuration */
static int set_sample_rate(u32_t rate, int num) {
	aud_conf[num].sample_rate = rate;
	return OK;
}

/* Set stereo in configuration */
static int set_stereo(u32_t stereo, int num) {
	aud_conf[num].stereo = stereo;
	return OK;
}

/* Set sample bits in configuration */
static int set_bits(u32_t bits, int num) {
	aud_conf[num].nr_of_bits = bits;
	return OK;
}

/* Set fragment size in configuration */
static int set_frag_size(u32_t frag_size, int num) {
	if (frag_size > (sub_dev[num].DmaSize / sub_dev[num].NrOfDmaFragments) ||
		frag_size < sub_dev[num].MinFragmentSize) {
		return EINVAL;
	}
	aud_conf[num].fragment_size = frag_size;
	return OK;
}

/* Set frame sign in configuration */
static int set_sign(u32_t val, int num) {
	aud_conf[num].sign = val;
	return OK;
}

/* Get maximum fragment size */
static int get_max_frag_size(u32_t *val, int *len, int num) {
	*len = sizeof(*val);
	*val = (sub_dev[num].DmaSize / sub_dev[num].NrOfDmaFragments);
	return OK;
}

/* Return 1 if there are free buffers */
static int free_buf(u32_t *val, int *len, int num) {
	*len = sizeof(*val);
	if (sub_dev[num].BufLength == sub_dev[num].NrOfExtraBuffers)
		*val = 0;
	else
		*val = 1;
	return OK;
}

/* Get the current sample counter */
static int get_samples_in_buf(u32_t *result, int *len, int chan) {
	u32_t res;
	/* READ_DMA_CURRENT_ADDR */
	res = dev_read_dma_current(&dev.base, chan);
	*result = (u32_t)(sub_dev[chan].BufLength * 8192) + res;
	return OK;
}

/* ======= [Audio interface] Initialize data structure ======= */
int drv_init(void) {
	drv.DriverName = DRIVER_NAME;
	drv.NrOfSubDevices = 3;
	drv.NrOfSpecialFiles = 3;
	//snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV, snd_dma_pci_data(&dev->pci), 64*1024, 256*1024);
	sub_dev[DAC].readable = 0;
	sub_dev[DAC].writable = 1;
	sub_dev[DAC].DmaSize = 64 * 1024;
	sub_dev[DAC].NrOfDmaFragments = 2;
	sub_dev[DAC].MinFragmentSize = 1024;
	sub_dev[DAC].NrOfExtraBuffers = 4;

	sub_dev[ADC].readable = 1;
	sub_dev[ADC].writable = 0;
	sub_dev[ADC].DmaSize = 64 * 1024;
	sub_dev[ADC].NrOfDmaFragments = 2;
	sub_dev[ADC].MinFragmentSize = 1024;
	sub_dev[ADC].NrOfExtraBuffers = 4;

	sub_dev[MIX].writable = 0;
	sub_dev[MIX].readable = 0;

	special_file[0].minor_dev_nr = 0;
	special_file[0].write_chan = DAC;
	special_file[0].read_chan = NO_CHANNEL;
	special_file[0].io_ctl = DAC;

	special_file[1].minor_dev_nr = 1;
	special_file[1].write_chan = NO_CHANNEL;
	special_file[1].read_chan = ADC;
	special_file[1].io_ctl = ADC;

	special_file[2].minor_dev_nr = 2;
	special_file[2].write_chan = NO_CHANNEL;
	special_file[2].read_chan = NO_CHANNEL;
	special_file[2].io_ctl = MIX;

	FUNC_LOG();
	return OK;
}

void dev_set_region(void){
	snd_mychip_region region;

	region = &dev.ba0_region.name.ba0;
	strcpy(region->name, "MYCHIP_BA0");
	region->base = dev.base[0];
	region->size = MYCHIP_BA0_SIZE;
	region->remap_addr = dev.remap_base[0];

	region = &dev.ba1_region.name.data0;
	strcpy(region->name, "MYCHIP_BA1_data0");
	region->base = dev.base[1] + BA1_SP_DMEM0;
	region->remap_addr = dev.remap_base[0] + BA1_SP_DMEM0;
	region->size = MYCHIP_BA1_DATA0_SIZE;

	region = &dev.ba1_region.name.data1;
	strcpy(region->name, "MYCHIP_BA1_data1");
	region->base = dev.base[1] + BA1_SP_DMEM1;
	region->remap_addr = dev.remap_base[0] + BA1_SP_DMEM1;
	region->size = MYCHIP_BA1_DATA1_SIZE;

	region = &dev.ba1_region.name.pmem;
	strcpy(region->name, "MYCHIP_BA1_pmem");
	region->base = dev.base[1] + BA1_SP_PMEM;
	region->remap_addr = dev.remap_base[0] + BA1_SP_PMEM;
	region->size = MYCHIP_BA1_PRG_SIZE;

	region = &dev.ba1_region.name.reg;
	strcpy(region->name, "MYCHIP_BA1_reg");
	region->base = dev.base[1] + BA1_SP_REG;
	region->remap_addr = dev.remap_base[0] + BA1_SP_REG;
	region->size = MYCHIP_BA1_REG_SIZE;
}

/* ======= [Audio interface] Initialize hardware ======= */
int drv_init_hw(void) {
	int i;

	/* Match the device */
	if (&dev_probe()) {
		printf("SDR: No sound card found\n");
		return EIO;
	}

	dev_set_region();

	/* init the device */
	if (&dev_init(&dev)!=OK) {
		printf("SDR: Fail to init the device\n");
		return EIO;
	}

	/* Configure the hardware */
	// dev_configure(&dev.base);

	/* Initialize the mixer */
	/* ### INIT_MIXER ### */

	dev_init_mixer(&dev.base);

	/* Set default mixer volume */
	dev_set_default_volume(&dev.base);

	/* Initialize subdevice data */
	for (i = 0; i < drv.NrOfSubDevices; i++) {
		if (i == MIX)
			continue;
		aud_conf[i].busy = 0;
		aud_conf[i].stereo = 1;
		aud_conf[i].sample_rate = 44100;
		aud_conf[i].nr_of_bits = 16;
		aud_conf[i].sign = 1;
		aud_conf[i].fragment_size =
			sub_dev[i].DmaSize / sub_dev[i].NrOfDmaFragments;
	}
	return OK;
}

/* ======= [Audio interface] Driver reset =======*/
int drv_reset(void) {
	/* ### RESET_HARDWARE_CAN_FAIL ### */
	return dev_init(&dev );
}

/* ======= [Audio interface] Driver start ======= */
int drv_start(int sub_dev, int DmaMode) {
	int sample_count;

	unsigned int tmp;
	/*
	 *  Reset the processor.
	 */
	snd_mychip_reset(&dev);
	/*
	 *  Download the image to the processor.
	 */
	/* old image */
	if (snd_mychip_download_image(&dev) < 0) {
		snd_printk(KERN_ERR "image download error\n");
		return -EIO;
	}
	FUNC_LOG();
	/* Set DAC and ADC sample rate */
	/* ### SET_SAMPLE_RATE ### */
	dev_set_playback_sample_rate(&dev.base, aud_conf[sub_dev].sample_rate);
	dev_set_capture_sample_rate(&dev.base, aud_conf[sub_dev].sample_rate);

	sample_count = aud_conf[sub_dev].fragment_size;
#ifdef DMA_LENGTH_BY_FRAME
	sample_count = sample_count / (aud_conf[sub_dev].nr_of_bits * (aud_conf[sub_dev].stereo + 1) / 8);
#endif
	/* Set DAC and ADC format */
	/* ### SET_FORMAT ### */
	dev_set_format(&dev.base, aud_conf[sub_dev].nr_of_bits,
			aud_conf[sub_dev].sign, aud_conf[sub_dev].stereo, sample_count);

	drv_reenable_int(sub_dev);

	/* Start the channel */
	/* ### START_CHANNEL ### */

	//dev_start_channel(&dev, sub_dev);

	aud_conf[sub_dev].busy = 1;

	return OK;
}


/* ======= [Audio interface] Driver start ======= */
int drv_stop(int sub_dev) {
	u32_t data;

	/* INTR_ENABLE_DISABLE */
	dev_intr_enable(&dev, INTR_DISABLE);

	/* ### STOP_CHANNEL ### */
	//dev_stop_channel(&dev.base, sub_dev);

	aud_conf[sub_dev].busy = 0;
	return OK;
}

/* ======= [Audio interface] Enable interrupt ======= */
int drv_reenable_int(int chan) {
	/* INTR_ENABLE_DISABLE */
	dev_intr_enable(&dev, INTR_ENABLE);
	return OK;
}

/* ======= [Audio interface] I/O control ======= */
int drv_io_ctl(unsigned long request, void *val, int *len, int sub_dev) {
	int status;
	switch (request) {
		case DSPIORATE:
			status = set_sample_rate(*((u32_t *)val), sub_dev);
			break;
		case DSPIOSTEREO:
			status = set_stereo(*((u32_t *)val), sub_dev);
			break;
		case DSPIOBITS:
			status = set_bits(*((u32_t *)val), sub_dev);
			break;
		case DSPIOSIZE:
			status = set_frag_size(*((u32_t *)val), sub_dev);
			break;
		case DSPIOSIGN:
			status = set_sign(*((u32_t *)val), sub_dev);
			break;
		case DSPIOMAX:
			status = get_max_frag_size(val, len, sub_dev);
			break;
		case DSPIORESET:
			status = drv_reset();
			break;
		case DSPIOFREEBUF:
			status = free_buf(val, len, sub_dev);
			break;
		case DSPIOSAMPLESINBUF:
			status = get_samples_in_buf(val, len, sub_dev);
			break;
		case DSPIOPAUSE:
			status = drv_pause(sub_dev);
			break;
		case DSPIORESUME:
			status = drv_resume(sub_dev);
			break;
		case MIXIOGETVOLUME:
			/* ### GET_SET_VOLUME ### */
			status = get_set_volume(&dev.base, val, GET_VOL);
			break;
		case MIXIOSETVOLUME:
			/* ### GET_SET_VOLUME ### */
			status = get_set_volume(&dev.base, val, SET_VOL);
			break;
		default:
			status = EINVAL;
			break;
	}
	return status;
}

/* ======= [Audio interface] Get request number ======= */
int drv_get_irq(char *irq) {
	*irq = dev.irq;
	return OK;
}

/* ======= [Audio interface] Get fragment size ======= */
int drv_get_frag_size(u32_t *frag_size, int sub_dev) {
	*frag_size = aud_conf[sub_dev].fragment_size;
	return OK;
}

/* ======= [Audio interface] Set DMA channel ======= */
int drv_set_dma(u32_t dma, u32_t length, int chan) {
#ifdef DMA_LENGTH_BY_FRAME
	length = length / (aud_conf[chan].nr_of_bits * (aud_conf[chan].stereo + 1) / 8);
#endif
	/* ### SET_DMA ### */
	dev_set_dma(&dev.base, dma, length, chan);
	return OK;
}

/* ======= [Audio interface] Get interrupt summary status ======= */
int drv_int_sum(void) {
	u32_t status;
	/* ### READ_CLEAR_INTR_STS ### */
	status = dev_read_clear_intr_status(&dev);
	dev.intr_status = status;
#ifdef MY_DEBUG
	printf("SDR: Interrupt status is 0x%08x\n", status);
#endif
	//return (status & (INTR_STS_DAC | INTR_STS_ADC));
	return (status & (HISR_VC0 | HISR_VC1));
}

/* ======= [Audio interface] Handle interrupt status ======= */
int drv_int(int sub_dev) {
	u32_t mask;

	/* ### CHECK_INTR_DAC ### */
	if (sub_dev == DAC)
		mask = HISR_VC0;
	/* ### CHECK_INTR_ADC ### */
	else if (sub_dev == ADC)
		mask = HISR_VC1;
	else
		return 0;

	return dev.intr_status & mask;
}

/* ======= [Audio interface] Pause DMA ======= */
int drv_pause(int sub_dev) {
	/* ### PAUSE_DMA ### */
	dev_pause_dma(&dev.base, sub_dev);
	return OK;
}

/* ======= [Audio interface] Resume DMA ======= */
int drv_resume(int sub_dev) {
	/* ### RESUME_DMA ### */
	
	dev_resume_dma(&dev.base, sub_dev);
	return OK;
}
