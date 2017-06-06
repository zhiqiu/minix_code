#ifndef _CS4624_H_
#define _CS4624_H_
/* ======= General Parameter ======= */
/* Global configure */
#define DMA_LENGTH_BY_FRAME
#define DMA_BASE_IOMAP
#define MIXER_AC97

#include <minix/audio_fw.h>
#include <sys/types.h>
#include <sys/ioc_sound.h>
#include <minix/sound.h>
#include <machine/pci.h>
#include <sys/mman.h>
#include "io.h"

/* Subdevice type */
#define DAC		0
#define ADC		1
#define MIX		2

/* PCI number and driver name */
#define VENDOR_ID		0x1013
#define DEVICE_ID		0x6003
#define DRIVER_NAME		"CS4624"

/* Volume option */
#define GET_VOL			0
#define SET_VOL			1

/* Interrupt control */
#define INTR_ENABLE		1
#define INTR_DISABLE	0

#define HICR_IEV                                0x00000001
#define HICR_CHGM                               0x00000002

/* Interrupt status */
#define INTR_STS_DAC		0x0100
#define INTR_STS_ADC		0x0200

/* ======= Self-defined Parameter ======= */
#define BA0_HISR				0x00000000  /* Host Interrupt Status Register */
// #define BA0_HSR0                0x00000004  /* */
#define BA0_HICR                0x00000008  /* Host Interrupt Control Register */





#define REG_CONF_WRITE		0x03e0
#define REG_POWER_EXT		0x03e4
#define REG_SPOWER_CTRL		0x03ec
#define REG_CONF_LOAD		0x03f0
#define REG_CLK_CTRL		0x0400
#define REG_MASTER_CTRL		0x0420
#define REG_CODEC_CTRL		0x0460
#define REG_CODEC_STATUS	0x0464
#define REG_CODEC_OSV		0x0468
#define REG_CODEC_ADDR		0x046c
#define REG_CODEC_DATA		0x0470
#define REG_CODEC_SDA		0x047c
#define REG_SOUND_POWER		0x0740
#define REG_DAC_SAMPLE_RATE	0x0744
#define REG_ADC_SAMPLE_RATE	0x0748
#define REG_SRC_SLOT		0x075c
#define REG_PCM_LVOL		0x0760
#define REG_PCM_RVOL		0x0764

#define REG_DAC_HDSR		0x00f0
#define REG_DAC_DCC			0x0114
#define REG_DAC_DMR			0x0150
#define REG_DAC_DCR			0x0154
#define REG_DAC_FCR			0x0180
#define REG_DAC_FSIC		0x0214
#define REG_ADC_HDSR		0x00f4
#define REG_ADC_DCC			0x0124
#define REG_ADC_DMR			0x0158
#define REG_ADC_DCR			0x015c
#define REG_ADC_FCR			0x0184
#define REG_ADC_FSIC		0x0214

#define REG_DAC_DMA_ADDR	0x0118
#define REG_DAC_DMA_LEN		0x011c
#define REG_ADC_DMA_ADDR	0x0128
#define REG_ADC_DMA_LEN		0x012c

#define CODEC_REG_POWER		0x26

#define STS_CODEC_DONE		0x0008
#define STS_CODEC_VALID		0x0002

#define CMD_POWER_DOWN		(1 << 14)
#define CMD_PORT_TIMING		(1 << 16)
#define CMD_AC97_MODE		(1 << 1)
#define CMD_MASTER_SERIAL	(1 << 0)
#define CMD_INTR_ENABLE		0x03
#define CMD_INTR_DMA		0x00040000
#define CMD_INTR_DMA0		0x0100
#define CMD_INTR_DMA1		0x0200
#define CMD_DMR_INIT		0x50
#define CMD_DMR_WRITE		0x08
#define CMD_DMR_READ		0x04
#define CMD_DMR_BIT8		(1 << 16)
#define CMD_DMR_MONO		(1 << 17)
#define CMD_DMR_UNSIGN		(1 << 19)
#define CMD_DMR_BIT32		(1 << 20)
#define CMD_DMR_SWAP		(1 << 22)
#define CMD_DMR_POLL		(1 << 28)
#define CMD_DMR_DMA			(1 << 29)
#define CMD_DCR_MASK		(1 << 0)
#define CMD_FCR_FEN			(1 << 31)
#define CMD_DAC_FCR_INIT	0x01002000
#define CMD_ADC_FCR_INIT	0x0b0a2020


#define MYCHIP_BA0_SIZE         0x1000
#define MYCHIP_BA1_DATA0_SIZE 	0x3000
#define MYCHIP_BA1_DATA1_SIZE	0x3800
#define MYCHIP_BA1_PRG_SIZE     0x7000
#define MYCHIP_BA1_REG_SIZE     0x0100

static u32_t dcr_data, dmr_data, fcr_data;
static u32_t g_sample_rate[] = {
	48000, 44100, 22050, 16000, 11025, 8000
};

/* Driver Data Structure */
typedef struct aud_sub_dev_conf_t {
	u32_t stereo;
	u16_t sample_rate;
	u32_t nr_of_bits;
	u32_t sign;
	u32_t busy;
	u32_t fragment_size;
	u8_t format;
} aud_sub_dev_conf_t;

struct snd_mychip_region{
	char name[24];
	u32_t base;
	u32_t remap_addr;   //虚拟地址，系统可访问  
	u32_t size;
};

// base[6]
// ba0
typedef struct DEV_STRUCT {
	char *name;
	u16_t vid;
	u16_t did;
	u32_t devind;
	u32_t base[6];
	u32_t remap_base[6];   //iomap后的虚拟地址

	union {
		struct {
			struct snd_mychip_region ba0;
		} name;
		struct snd_mychip_region idx[1];     // 用name和idx访问是等价的
	} ba0_region;                            // ba0_addr映射的region

	union {
		struct {
			struct snd_mychip_region data0;
			struct snd_mychip_region data1;
			struct snd_mychip_region pmem;
			struct snd_mychip_region reg;
		} name;
		struct snd_mychip_region idx[4];     // 用name和idx访问是等价的
	} ba1_region;							 // ba1_addr映射的region

	char irq;
	char revision;
	u32_t intr_status;
	u32_t play_ctl;
	u32_t capt_ctl;
} DEV_STRUCT;

void dev_mixer_write(DEV_STRUCT *dev, u32_t reg, u32_t val);
u32_t dev_mixer_read(DEV_STRUCT *dev, u32_t reg);
// u32_t == usigned long
// u16_t == usigned int
// u8_t  == usigned char
void snd_mychip_pokeBA1(DEV_STRUCT *dev, u32_t reg, u32_t val){
	u16_t bank = reg >> 16;
	u16_t offset = reg & 0xffff;
	sdr_out32(dev->ba1_region.idx[bank].remap_addr, offset, val);
}

u32_t snd_mychip_peekBA1(DEV_STRUCT *dev, u32_t reg){
	u16_t bank = reg >> 16;
	u16_t offset = reg & 0xffff;
	return sdr_in32(dev->ba1_region.idx[bank].remap_addr, offset);
}

void snd_mychip_pokeBA0(DEV_STRUCT *dev, u32_t offset, u32_t val){
	sdr_out32(dev->ba0_region.idx[0].remap_addr, offset, val);
}

u32_t snd_mychip_peekBA0(DEV_STRUCT *dev, u32_t offset){
	return sdr_in32(dev->ba0_region.idx[0].remap_addr, offset);
}

#endif
