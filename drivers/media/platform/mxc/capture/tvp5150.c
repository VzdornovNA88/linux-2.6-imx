/*
 * Copyright 2005-2014 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/*!
 * @file tvp5150.c
 *
 * @brief TVP515x video decoder functions
 *
 * @ingroup Camera
 */
//#define DEBUG
#include <dt-bindings/media/tvp5150.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-chip-ident.h>
#include <media/v4l2-of.h>
//#include <media/v4l2-int-device.h>
#include "v4l2-int-device.h"
#include "mxc_v4l2_capture.h"

#include "tvp5150_reg.h"

/* Module Name */
#define TVP5150_MODULE_NAME		"tvp5150"

#define TVP5150_H_MAX		720U
//#define TVP5150_V_MAX_525_60	480U
//#define TVP5150_V_MAX_OTHERS	576U
#define TVP5150_V_MAX_525_60	525U
#define TVP5150_V_MAX_OTHERS	625U
#define TVP5150_MAX_CROP_LEFT	511
#define TVP5150_MAX_CROP_TOP	127
#define TVP5150_CROP_SHIFT	2

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-2)");

/**
 * enum tvp5150_std - enum for supported standards
 */
enum tvp5150_std {
	STD_NTSC_MJ = 0,
	STD_PAL_BDGHIN,
	STD_PAL_M,
	STD_PAL_Nc,
	STD_NTSC_443,
	STD_SECAM,
	STD_INVALID
};

/**
 * struct tvp514x_std_info - Structure to store standard informations
 * @width: Line width in pixels
 * @height:Number of active lines
 * @video_std: Value to write in REG_VIDEO_STD register
 * @standard: v4l2 standard structure information
 */
struct tvp5150_std_info {
	unsigned long width;
	unsigned long height;
	u8 video_std_bit;
	struct v4l2_standard standard;
};


/**
 * struct tvp514x_decoder - TVP5150 decoder object
 * @v4l2_int_device: Slave handle
 * @tvp5150_slave: Slave pointer which is used by @v4l2_int_device
 * @tvp5150_regs: copy of hw's regs with preset values.
 * @pdata: Board specific
 * @client: I2C client data
 * @id: Entry from I2C table
 * @rom_ver: Chip version
 * @state: TVP5150 decoder state - detected or not-detected
 * @pix: Current pixel format
 * @num_fmts: Number of formats
 * @fmt_list: Format list
 * @current_std: Current standard
 * @num_stds: Number of standards
 * @std_list: Standards list
 * @route: input and output routing at chip level
 */
struct tvp5150_decoder {
	struct sensor_data sen;
	struct v4l2_int_slave tvp5150_slave;
	struct v4l2_int_device v4l2_int_device;
	struct v4l2_rect rect;
//	struct i2c_client *i2c_client;

	struct i2c_device_id *id;

	v4l2_std_id std_id;	/* Current set standard */
	struct v4l2_routing route;
	//u32 input;
	//u32 output;
	int enable;

	u16 dev_id;
	u16 rom_ver;

//	struct v4l2_pix_format pix;
	int num_fmts;
	const struct v4l2_fmtdesc *fmt_list;

	enum tvp5150_std current_std;
	int num_stds;
	struct tvp5150_std_info *std_list;

	u8  debug_reg_num;
};

/* List of image formats supported by TVP5150 decoder
 * Using 8 bit mode only.
 */
static const struct v4l2_fmtdesc tvp5150_fmt_list[] = {
	{
	 .index = 0,
	 .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
	 .flags = 0,
	 .description = "8-bit UYVY 4:2:2 Format",
	 .pixelformat = V4L2_PIX_FMT_UYVY,
	},
//	{
//	 .index = 1,
//	 .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
//	 .flags = 0,
//	 .description = "8-bit YUYV 4:2:2 Format",
//	 .pixelformat = V4L2_PIX_FMT_YUYV,
//	},
};

/*
 * Supported standards -
 *
 * Currently supports two standards only, need to add support for rest of the
 * modes, like SECAM, etc...
 */
static struct tvp5150_std_info tvp5150_std_list[] = {
	/* Standard: STD_NTSC_MJ */
	[STD_NTSC_MJ] = {
	 .width = TVP5150_H_MAX,
	 .height = TVP5150_V_MAX_525_60,
	 .video_std_bit = VIDEO_STD_NTSC_MJ_BIT_AS,
	 .standard = {
		      .index = 0,
		      .id = V4L2_STD_NTSC,
		      .name = "NTSC M/J",
		      .frameperiod = {1001, 30000},
		      .framelines = 525
		     },
	},
	/* Standard: STD_PAL_BDGHIN */
	[STD_PAL_BDGHIN] = {
	 .width = TVP5150_H_MAX,
	 .height = TVP5150_V_MAX_OTHERS,
	 .video_std_bit = VIDEO_STD_PAL_BDGHIN_BIT_AS,
	 .standard = {
		      .index = 1,
		      .id = 	V4L2_STD_PAL_BG |
				V4L2_STD_PAL_D  |
				V4L2_STD_PAL_D1 |
				V4L2_STD_PAL_H	|
				V4L2_STD_PAL_I	|
				V4L2_STD_PAL_N ,
		      .name = "PAL B/D/G/H/I/N",
		      .frameperiod = {1, 25},
		      .framelines = 625
		     },
	},
	/* Standard: STD_PAL_M */
	[STD_PAL_M] = {
	 .width = TVP5150_H_MAX,
	 .height = TVP5150_V_MAX_525_60,
	 .video_std_bit = VIDEO_STD_PAL_M_BIT_AS,
	 .standard = {
		      .index = 2,
		      .id = V4L2_STD_PAL_M,
		      .name = "PAL M",
		      .frameperiod = {1001, 30000},
		      .framelines = 525
		     },
	},
	/* Standard: STD_PAL_Nc */
	[STD_PAL_Nc] = {
	 .width = TVP5150_H_MAX,
	 .height = TVP5150_V_MAX_OTHERS,
	 .video_std_bit = VIDEO_STD_PAL_COMBINATION_N_BIT_AS,
	 .standard = {
		      .index = 3,
		      .id = V4L2_STD_PAL_Nc,
		      .name = "PAL-Nc",
		      .frameperiod = {1, 25},
		      .framelines = 625
		     },
	},
	/* Standard: STD_NTSC_443 */
	[STD_NTSC_443] = {
	 .width = TVP5150_H_MAX,
	 .height = TVP5150_V_MAX_525_60,
	 .video_std_bit = VIDEO_STD_NTSC_4_43_BIT_AS,
	 .standard = {
		      .index = 4,
		      .id = V4L2_STD_NTSC_443,
		      .name = "NTSC 4.43",
		      .frameperiod = {1001, 30000},
		      .framelines = 525
		     },
	},
	/* Standard: STD_SECAM */
	[STD_SECAM] = {
	 .width = TVP5150_H_MAX,
	 .height = TVP5150_V_MAX_OTHERS,
	 .video_std_bit = VIDEO_STD_SECAM_BIT_AS,
	 .standard = {
		      .index = 5,
		      .id = V4L2_STD_SECAM,
		      .name = "SECAM",
		      .frameperiod = {1, 25},
		      .framelines = 625
		     },
	},
	/* Standard: need to add for additional standard */
};

//static inline struct tvp5150_decoder *to_tvp5150(struct v4l2_subdev *sd)
//{
//	return container_of(sd, struct tvp5150_decoder, sd);
//}

//static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
//{
//	return &container_of(ctrl->handler, struct tvp5150_decoder, hdl)->sd;
//}

/*! @brief This mutex is used to provide mutual exclusion.
 *
 *  Create a mutex that can be used to provide mutually exclusive
 *  read/write access to the globally accessible data structures
 *  and variables that were defined above.
 */
static DEFINE_MUTEX(mutex);

/* supported controls */
/* This hasn't been fully implemented yet.
 * This is how it should work, though. */
static struct v4l2_queryctrl tvp5150_qctrl[] = {
	{
	.id = V4L2_CID_BRIGHTNESS,
	.type = V4L2_CTRL_TYPE_INTEGER,
	.name = "Brightness",
	.minimum = 0,		/* check this value */
	.maximum = 255,		/* check this value */
	.step = 1,		/* check this value */
	.default_value = 128,	/* check this value */
	.flags = 0,
	}, {
	.id = V4L2_CID_CONTRAST,
	.type = V4L2_CTRL_TYPE_INTEGER,
	.name = "Contrast",
	.minimum = 0,		/* check this value */
	.maximum = 255,		/* check this value */
	.step = 1,		/* check this value */
	.default_value = 128,	/* check this value */
	.flags = 0,
	}, {
	.id = V4L2_CID_SATURATION,
	.type = V4L2_CTRL_TYPE_INTEGER,
	.name = "Saturation",
	.minimum = 0,		/* check this value */
	.maximum = 255,		/* check this value */
	.step = 1,		/* check this value */
	.default_value = 128,	/* check this value */
	.flags = 0,
	}, {
	.id = V4L2_CID_HUE,
	.type = V4L2_CTRL_TYPE_INTEGER,
	.name = "Hue",
	.minimum =-128,		/* check this value */
	.maximum = 127,		/* check this value */
	.step = 1,		/* check this value */
	.default_value = 0,	/* check this value */
	.flags = 0,
	}
};

/***********************************************************************
 * I2C transfert.
 ***********************************************************************/

/*! Read one register from a tvp5150 i2c slave device.
 *
 *  @param *addr		register in the device we wish to access.
 *
 *  @return		       0 if success, an error code otherwise.
 */
static inline int tvp5150_read(struct v4l2_int_device *s, unsigned char addr)
{
	struct tvp5150_decoder *decoder = s->priv;
	struct i2c_client *c = decoder->sen.i2c_client; //v4l2_get_subdevdata(sd);
	int rc;

	rc = i2c_smbus_read_byte_data(c, addr);
	if (rc < 0) {
		v4l_err(c,
			"i2c i/o error: rc == %d\n", rc);
		return rc;
	}

	v4l_dbg(2, debug, c, 
		"tvp5150: read 0x%02x = 0x%02x\n", addr, rc);

	return rc;
}

/*! Write one register of a tvp5150 i2c slave device.
 *
 *  @param *addr		register in the device we wish to access.
 *
 *  @return		       0 if success, an error code otherwise.
 */
static int tvp5150_write(struct v4l2_int_device *s, unsigned char addr, unsigned char value)
{
	struct tvp5150_decoder *decoder = s->priv;
	struct i2c_client *c = decoder->sen.i2c_client; //v4l2_get_subdevdata(sd);
	int rc;

	v4l_dbg(2, debug, c,
		 "tvp5150: writing 0x%02x 0x%02x\n", addr, value);
	rc = i2c_smbus_write_byte_data(c, addr, value);
	if (rc < 0) {
		v4l_err(c,
			"i2c i/o error: rc == %d\n", rc);
		return rc;
	}
	return 0;
}

static void dump_reg_range(struct v4l2_int_device *s, char *str, u8 init,
				const u8 end, int max_line)
{
	int i = 0;

	while (init != (u8)(end + 1)) {
		if ((i % max_line) == 0) {
			if (i > 0)
				printk("\n");
			printk("tvp5150: %s reg 0x%02x = ", str, init);
		}
		printk("%02x ", tvp5150_read(s,init));

		init++;
		i++;
	}
	printk("\n");
}

static int tvp5150_log_status(struct v4l2_int_device *s)
{
	printk("tvp5150: Video input source selection #1 = 0x%02x\n",
			tvp5150_read(s,TVP5150_VD_IN_SRC_SEL_1));
	printk("tvp5150: Analog channel controls = 0x%02x\n",
			tvp5150_read(s,TVP5150_ANAL_CHL_CTL));
	printk("tvp5150: Operation mode controls = 0x%02x\n",
			tvp5150_read(s,TVP5150_OP_MODE_CTL));
	printk("tvp5150: Miscellaneous controls = 0x%02x\n",
			tvp5150_read(s,TVP5150_MISC_CTL));
	printk("tvp5150: Autoswitch mask= 0x%02x\n",
			tvp5150_read(s,TVP5150_AUTOSW_MSK));
	printk("tvp5150: Color killer threshold control = 0x%02x\n",
			tvp5150_read(s,TVP5150_COLOR_KIL_THSH_CTL));
	printk("tvp5150: Luminance processing controls #1 #2 and #3 = %02x %02x %02x\n",
			tvp5150_read(s,TVP5150_LUMA_PROC_CTL_1),
			tvp5150_read(s,TVP5150_LUMA_PROC_CTL_2),
			tvp5150_read(s,TVP5150_LUMA_PROC_CTL_3));
	printk("tvp5150: Brightness control = 0x%02x\n",
			tvp5150_read(s,TVP5150_BRIGHT_CTL));
	printk("tvp5150: Color saturation control = 0x%02x\n",
			tvp5150_read(s,TVP5150_SATURATION_CTL));
	printk("tvp5150: Hue control = 0x%02x\n",
			tvp5150_read(s,TVP5150_HUE_CTL));
	printk("tvp5150: Contrast control = 0x%02x\n",
			tvp5150_read(s,TVP5150_CONTRAST_CTL));
	printk("tvp5150: Outputs and data rates select = 0x%02x\n",
			tvp5150_read(s,TVP5150_DATA_RATE_SEL));
	printk("tvp5150: Configuration shared pins = 0x%02x\n",
			tvp5150_read(s,TVP5150_CONF_SHARED_PIN));
	printk("tvp5150: Active video cropping start = 0x%02x%02x\n",
			tvp5150_read(s,TVP5150_ACT_VD_CROP_ST_MSB),
			tvp5150_read(s,TVP5150_ACT_VD_CROP_ST_LSB));
	printk("tvp5150: Active video cropping stop  = 0x%02x%02x\n",
			tvp5150_read(s,TVP5150_ACT_VD_CROP_STP_MSB),
			tvp5150_read(s,TVP5150_ACT_VD_CROP_STP_LSB));
	printk("tvp5150: Genlock/RTC = 0x%02x\n",
			tvp5150_read(s,TVP5150_GENLOCK));
	printk("tvp5150: Horizontal sync start = 0x%02x\n",
			tvp5150_read(s,TVP5150_HORIZ_SYNC_START));
	printk("tvp5150: Vertical blanking start = 0x%02x\n",
			tvp5150_read(s,TVP5150_VERT_BLANKING_START));
	printk("tvp5150: Vertical blanking stop = 0x%02x\n",
			tvp5150_read(s,TVP5150_VERT_BLANKING_STOP));
	printk("tvp5150: Chrominance processing control #1 and #2 = %02x %02x\n",
			tvp5150_read(s,TVP5150_CHROMA_PROC_CTL_1),
			tvp5150_read(s,TVP5150_CHROMA_PROC_CTL_2));
	printk("tvp5150: Interrupt reset register B = 0x%02x\n",
			tvp5150_read(s,TVP5150_INT_RESET_REG_B));
	printk("tvp5150: Interrupt enable register B = 0x%02x\n",
			tvp5150_read(s,TVP5150_INT_ENABLE_REG_B));
	printk("tvp5150: Interrupt configuration register B = 0x%02x\n",
			tvp5150_read(s,TVP5150_INTT_CONFIG_REG_B));
	printk("tvp5150: Video standard = 0x%02x\n",
			tvp5150_read(s,TVP5150_VIDEO_STD));
	printk("tvp5150: Chroma gain factor: Cb=0x%02x Cr=0x%02x\n",
			tvp5150_read(s,TVP5150_CB_GAIN_FACT),
			tvp5150_read(s,TVP5150_CR_GAIN_FACTOR));
	printk("tvp5150: Macrovision on counter = 0x%02x\n",
			tvp5150_read(s,TVP5150_MACROVISION_ON_CTR));
	printk("tvp5150: Macrovision off counter = 0x%02x\n",
			tvp5150_read(s,TVP5150_MACROVISION_OFF_CTR));
	printk("tvp5150: ITU-R BT.656.%d timing(TVP5150AM1 only)\n",
			(tvp5150_read(s,TVP5150_REV_SELECT) & 1) ? 3 : 4);
	printk("tvp5150: Device ID = %02x%02x\n",
			tvp5150_read(s,TVP5150_MSB_DEV_ID),
			tvp5150_read(s,TVP5150_LSB_DEV_ID));
	printk("tvp5150: ROM version = (hex) %02x.%02x\n",
			tvp5150_read(s,TVP5150_ROM_MAJOR_VER),
			tvp5150_read(s,TVP5150_ROM_MINOR_VER));
	printk("tvp5150: Vertical line count = 0x%02x%02x\n",
			tvp5150_read(s,TVP5150_VERT_LN_COUNT_MSB),
			tvp5150_read(s,TVP5150_VERT_LN_COUNT_LSB));
	printk("tvp5150: Interrupt status register B = 0x%02x\n",
			tvp5150_read(s,TVP5150_INT_STATUS_REG_B));
	printk("tvp5150: Interrupt active register B = 0x%02x\n",
			tvp5150_read(s,TVP5150_INT_ACTIVE_REG_B));
	printk("tvp5150: Status regs #1 to #5 = %02x %02x %02x %02x %02x\n",
			tvp5150_read(s,TVP5150_STATUS_REG_1),
			tvp5150_read(s,TVP5150_STATUS_REG_2),
			tvp5150_read(s,TVP5150_STATUS_REG_3),
			tvp5150_read(s,TVP5150_STATUS_REG_4),
			tvp5150_read(s,TVP5150_STATUS_REG_5));

	dump_reg_range(s,"Teletext filter 1",   TVP5150_TELETEXT_FIL1_INI,
			TVP5150_TELETEXT_FIL1_END, 8);
	dump_reg_range(s,"Teletext filter 2",   TVP5150_TELETEXT_FIL2_INI,
			TVP5150_TELETEXT_FIL2_END, 8);

	printk("tvp5150: Teletext filter enable = 0x%02x\n",
			tvp5150_read(s,TVP5150_TELETEXT_FIL_ENA));
	printk("tvp5150: Interrupt status register A = 0x%02x\n",
			tvp5150_read(s,TVP5150_INT_STATUS_REG_A));
	printk("tvp5150: Interrupt enable register A = 0x%02x\n",
			tvp5150_read(s,TVP5150_INT_ENABLE_REG_A));
	printk("tvp5150: Interrupt configuration = 0x%02x\n",
			tvp5150_read(s,TVP5150_INT_CONF));
	printk("tvp5150: VDP status register = 0x%02x\n",
			tvp5150_read(s,TVP5150_VDP_STATUS_REG));
	printk("tvp5150: FIFO word count = 0x%02x\n",
			tvp5150_read(s,TVP5150_FIFO_WORD_COUNT));
	printk("tvp5150: FIFO interrupt threshold = 0x%02x\n",
			tvp5150_read(s,TVP5150_FIFO_INT_THRESHOLD));
	printk("tvp5150: FIFO reset = 0x%02x\n",
			tvp5150_read(s,TVP5150_FIFO_RESET));
	printk("tvp5150: Line number interrupt = 0x%02x\n",
			tvp5150_read(s,TVP5150_LINE_NUMBER_INT));
	printk("tvp5150: Pixel alignment register = 0x%02x%02x\n",
			tvp5150_read(s,TVP5150_PIX_ALIGN_REG_HIGH),
			tvp5150_read(s,TVP5150_PIX_ALIGN_REG_LOW));
	printk("tvp5150: FIFO output control = 0x%02x\n",
			tvp5150_read(s,TVP5150_FIFO_OUT_CTRL));
	printk("tvp5150: Full field enable = 0x%02x\n",
			tvp5150_read(s,TVP5150_FULL_FIELD_ENA));
	printk("tvp5150: Full field mode register = 0x%02x\n",
			tvp5150_read(s,TVP5150_FULL_FIELD_MODE_REG));

	dump_reg_range(s,"CC   data",   TVP5150_CC_DATA_INI,
			TVP5150_CC_DATA_END, 8);

	dump_reg_range(s,"WSS  data",   TVP5150_WSS_DATA_INI,
			TVP5150_WSS_DATA_END, 8);

	dump_reg_range(s,"VPS  data",   TVP5150_VPS_DATA_INI,
			TVP5150_VPS_DATA_END, 8);

	dump_reg_range(s,"VITC data",   TVP5150_VITC_DATA_INI,
			TVP5150_VITC_DATA_END, 10);

	dump_reg_range(s,"Line mode",   TVP5150_LINE_MODE_INI,
			TVP5150_LINE_MODE_END, 8);
	return 0;
}

static ssize_t tvp5150_reg_write(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf,
				 size_t count)
{
	struct tvp5150_decoder *decoder = dev_get_drvdata(dev);
	int reg, data;
	int cnt_params;

	if(!strncmp("all",buf,3)) {
		tvp5150_log_status(0);
		return 3;
	}
	cnt_params = sscanf(buf, "%x %x", &reg, &data);
	if (cnt_params<=0)
		return -EINVAL;

	decoder->debug_reg_num = reg;
	if(cnt_params == 2) {
		tvp5150_write(&decoder->v4l2_int_device, reg,data);
	}
	return count;
}

static ssize_t tvp5150_reg_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct tvp5150_decoder *decoder = dev_get_drvdata(dev);
	return scnprintf(buf,PAGE_SIZE,"0x%02x=0x%02x\n",
				decoder->debug_reg_num,tvp5150_read(&decoder->v4l2_int_device,
									decoder->debug_reg_num));
}

static DEVICE_ATTR(reg, S_IRUGO | S_IWUSR, tvp5150_reg_show, tvp5150_reg_write);

/****************************************************************************
			Basic functions
 ****************************************************************************/

static inline void tvp5150_selmux(struct v4l2_int_device *s)
{
	int opmode = 0;
	struct tvp5150_decoder *decoder = s->priv;
	u32 input = 0;
	int val;

	if ((decoder->route.output & TVP5150_BLACK_SCREEN) || !decoder->enable)
		input = 8;

	switch (decoder->route.input) {
	case TVP5150_COMPOSITE1:
		input |= 2;
		/* fall through */
	case TVP5150_COMPOSITE0:
		break;
	case TVP5150_SVIDEO:
	default:
		input |= 1;
		break;
	}

	dev_dbg(&decoder->sen.i2c_client->dev, 
			"Selecting video route: route input=%i, output=%i "
			"=> tvp5150 input=%i, opmode=%i\n",
			decoder->route.input, decoder->route.output,
			input, opmode);

	tvp5150_write(s,TVP5150_OP_MODE_CTL, opmode);
	tvp5150_write(s,TVP5150_VD_IN_SRC_SEL_1, input);

	/* Svideo should enable YCrCb output and disable GPCL output
	 * For Composite and TV, it should be the reverse
	 */
	val = tvp5150_read(s,TVP5150_MISC_CTL);
	if (val < 0) {
		v4l_err(decoder->sen.i2c_client, "%s: failed with error = %d\n", __func__, val);
		return;
	}

	if (decoder->route.input == TVP5150_SVIDEO)
		val = (val & ~0x40) | 0x10;
	else
		val = (val & ~0x10) | 0x40;
	tvp5150_write(s,TVP5150_MISC_CTL, val);
};

struct i2c_reg_value {
	unsigned char reg;
	unsigned char value;
};

/* Default values as sugested at TVP5150AM1 datasheet */
static const struct i2c_reg_value tvp5150_init_default[] = {
	{ /* 0x00 */
		TVP5150_VD_IN_SRC_SEL_1,0x00
	},
	{ /* 0x01 */
		TVP5150_ANAL_CHL_CTL,0x15
	},
	{ /* 0x02 */
		TVP5150_OP_MODE_CTL,0x00
	},
	{ /* 0x03 */
		TVP5150_MISC_CTL,0x01
	},
	{ /* 0x06 */
		TVP5150_COLOR_KIL_THSH_CTL,0x10
	},
	{ /* 0x07 */
		TVP5150_LUMA_PROC_CTL_1,0x60
	},
	{ /* 0x08 */
		TVP5150_LUMA_PROC_CTL_2,0x00
	},
	{ /* 0x09 */
		TVP5150_BRIGHT_CTL,0x80
	},
	{ /* 0x0a */
		TVP5150_SATURATION_CTL,0x80
	},
	{ /* 0x0b */
		TVP5150_HUE_CTL,0x00
	},
	{ /* 0x0c */
		TVP5150_CONTRAST_CTL,0x80
	},
	{ /* 0x0d */
		TVP5150_DATA_RATE_SEL,0x47
	},
	{ /* 0x0e */
		TVP5150_LUMA_PROC_CTL_3,0x00
	},
	{ /* 0x0f */
		TVP5150_CONF_SHARED_PIN,0x08
	},
	{ /* 0x11 */
		TVP5150_ACT_VD_CROP_ST_MSB,0x00
	},
	{ /* 0x12 */
		TVP5150_ACT_VD_CROP_ST_LSB,0x00
	},
	{ /* 0x13 */
		TVP5150_ACT_VD_CROP_STP_MSB,0x00
	},
	{ /* 0x14 */
		TVP5150_ACT_VD_CROP_STP_LSB,0x00
	},
	{ /* 0x15 */
		TVP5150_GENLOCK,0x01
	},
	{ /* 0x16 */
		TVP5150_HORIZ_SYNC_START,0x80
	},
	{ /* 0x18 */
		TVP5150_VERT_BLANKING_START,0x00
	},
	{ /* 0x19 */
		TVP5150_VERT_BLANKING_STOP,0x00
	},
	{ /* 0x1a */
		TVP5150_CHROMA_PROC_CTL_1,0x0c
	},
	{ /* 0x1b */
		TVP5150_CHROMA_PROC_CTL_2,0x14
	},
	{ /* 0x1c */
		TVP5150_INT_RESET_REG_B,0x00
	},
	{ /* 0x1d */
		TVP5150_INT_ENABLE_REG_B,0x00
	},
	{ /* 0x1e */
		TVP5150_INTT_CONFIG_REG_B,0x00
	},
	{ /* 0x28 */
		TVP5150_VIDEO_STD,0x00
	},
	{ /* 0x2e */
		TVP5150_MACROVISION_ON_CTR,0x0f
	},
	{ /* 0x2f */
		TVP5150_MACROVISION_OFF_CTR,0x01
	},
	{ /* 0xbb */
		TVP5150_TELETEXT_FIL_ENA,0x00
	},
	{ /* 0xc0 */
		TVP5150_INT_STATUS_REG_A,0x00
	},
	{ /* 0xc1 */
		TVP5150_INT_ENABLE_REG_A,0x00
	},
	{ /* 0xc2 */
		TVP5150_INT_CONF,0x04
	},
	{ /* 0xc8 */
		TVP5150_FIFO_INT_THRESHOLD,0x80
	},
	{ /* 0xc9 */
		TVP5150_FIFO_RESET,0x00
	},
	{ /* 0xca */
		TVP5150_LINE_NUMBER_INT,0x00
	},
	{ /* 0xcb */
		TVP5150_PIX_ALIGN_REG_LOW,0x4e
	},
	{ /* 0xcc */
		TVP5150_PIX_ALIGN_REG_HIGH,0x00
	},
	{ /* 0xcd */
		TVP5150_FIFO_OUT_CTRL,0x01
	},
	{ /* 0xcf */
		TVP5150_FULL_FIELD_ENA,0x00
	},
	{ /* 0xd0 */
		TVP5150_LINE_MODE_INI,0x00
	},
	{ /* 0xfc */
		TVP5150_FULL_FIELD_MODE_REG,0x7f
	},
	{ /* end of data */
		0xff,0xff
	}
};

/* Default values as sugested at TVP5150AM1 datasheet */
static const struct i2c_reg_value tvp5150_init_enable[] = {
	{
		TVP5150_CONF_SHARED_PIN, 2
	},{	/* Automatic offset and AGC enabled */
		TVP5150_ANAL_CHL_CTL, 0x15
	},{	/* Activate YCrCb output 0x9 or 0xd ? */
		TVP5150_MISC_CTL, 0x0d
	},{	/* Activates video std autodetection for all standards */
		TVP5150_AUTOSW_MSK, 0x0
	},{
		TVP5150_VIDEO_STD, VIDEO_STD_AUTO_SWITCH_BIT
	},{	/* Default format: 0x47. For 4:2:2: 0x40 */
		TVP5150_DATA_RATE_SEL, 0x47
	},{
		TVP5150_CHROMA_PROC_CTL_1, 0x0c
	},{
		TVP5150_CHROMA_PROC_CTL_2, 0x04
	},{	/* Non documented, but initialized on WinTV USB2 */
//		0x27, 0x20
//	},{
		0xff,0xff
	}
};

static int tvp5150_write_inittab(struct v4l2_int_device *s, const struct i2c_reg_value *regs)
{
	while (regs->reg != 0xff) {
		tvp5150_write(s,regs->reg, regs->value);
		regs++;
	}
	return 0;
}

/***********************************************************************
 * mxc_v4l2_capture interface.
 ***********************************************************************/
/*
 * tvp5150_get_current_std:
 * Returns the current standard detected by TVP5150
 */
static enum tvp5150_std tvp5150_get_current_std(struct tvp5150_decoder
						*decoder)
{
	u8 std_status;

	std_status = tvp5150_read(&decoder->v4l2_int_device, TVP5150_STATUS_REG_5);

	switch (std_status & 0x0F) {
	case VIDEO_STD_NTSC_MJ_BIT_AS:
		return STD_NTSC_MJ;

	case VIDEO_STD_PAL_BDGHIN_BIT_AS:
		return STD_PAL_BDGHIN;

	case VIDEO_STD_PAL_M_BIT_AS:
		return STD_PAL_M;

	case VIDEO_STD_PAL_COMBINATION_N_BIT_AS:
		return STD_PAL_Nc;

	case VIDEO_STD_NTSC_4_43_BIT_AS:
		return STD_NTSC_443;

	case VIDEO_STD_SECAM_BIT_AS:
		return STD_SECAM;

	default:
		return STD_INVALID;
	}

	return STD_INVALID;
}
#if 0
static v4l2_std_id tvp5150_read_std(struct v4l2_int_device *s)
{
	int val = tvp5150_read(s,TVP5150_STATUS_REG_5);

	switch (val & 0x0F) {
	case 0x01:
		return V4L2_STD_NTSC;
	case 0x03:
		return V4L2_STD_PAL;
	case 0x05:
		return V4L2_STD_PAL_M;
	case 0x07:
		return V4L2_STD_PAL_N | V4L2_STD_PAL_Nc;
	case 0x09:
		return V4L2_STD_NTSC_443;
	case 0xb:
		return V4L2_STD_SECAM;
	default:
		return V4L2_STD_UNKNOWN;
	}
}
#endif
/**
 * ioctl_querystd - V4L2 decoder interface handler for VIDIOC_QUERYSTD ioctl
 * @s: pointer to standard V4L2 device structure
 * @std_id: standard V4L2 std_id ioctl enum
 *
 * Returns the current standard detected by TVP5150. If no active input is
 * detected, returns -EINVAL
 */
static int ioctl_querystd(struct v4l2_int_device *s, v4l2_std_id *std_id)
{
	struct tvp5150_decoder *decoder = s->priv;
	enum tvp5150_std current_std;
	u32 input_sel;
	u8 sync_lock_status, lock_mask;

	dev_dbg(&decoder->sen.i2c_client->dev, "In tvp5150:ioctl_querystd\n");

	if (std_id == NULL)
		return -EINVAL;

	/* get the current standard */
	current_std = tvp5150_get_current_std(decoder);
	if (current_std == STD_INVALID) {
		current_std = V4L2_STD_UNKNOWN;
		return -EINVAL;
	}

#if 0
	input_sel = decoder->route.input;

	switch (input_sel) {
	case TVP5150_COMPOSITE0:
	case TVP5150_COMPOSITE1:
		lock_mask = 	0x08 /*STATUS_CLR_SUBCAR_LOCK_BIT*/ |
				0x02 /*STATUS_HORZ_SYNC_LOCK_BIT*/ |
				0x04 /*STATUS_VIRT_SYNC_LOCK_BIT*/;
		break;

	case TVP5150_SVIDEO:
		lock_mask = 	0x02 /*STATUS_HORZ_SYNC_LOCK_BIT*/ |
				0x04 /*STATUS_VIRT_SYNC_LOCK_BIT*/;
		break;
		/*Need to add other interfaces*/
	default:
		return -EINVAL;
	}
	/* check whether signal is locked */
	sync_lock_status = tvp5150_read(s, TVP5150_STATUS_REG_1);
	if (lock_mask != (sync_lock_status & lock_mask))
		return -EINVAL;	/* No input detected */
#endif
	decoder->current_std = current_std;
	*std_id = decoder->std_list[current_std].standard.id;

	dev_dbg(&decoder->sen.i2c_client->dev, "Current STD: %s",
			decoder->std_list[current_std].standard.name);
	return 0;
}

/*!
 * Return attributes of current video standard.
 * Since this device autodetects the current standard, this function also
 * sets the values that need to be changed if the standard changes.
 * There is no set std equivalent function.
 *
 *  @return		None.
 */
#if 0
static void tvp5150_get_std(struct v4l2_int_device *s, v4l2_std_id *std)
{
	struct tvp5150_decoder *decoder = s->priv;

	dev_dbg(&decoder->i2c_client->dev, "In tvp5150_get_std\n");
	//pr_info("tvp5150:tvp5150_get_std\n");

	*std = tvp5150_read_std(s);
	
	if (*std & V4L2_STD_525_60)
		decoder->pix.height = TVP5150_V_MAX_525_60;
	else
		decoder->pix.height = TVP5150_V_MAX_OTHERS;
	decoder->pix.width = TVP5150_H_MAX;

	decoder->std_id = *std;
}

static int tvp5150_set_std(struct v4l2_int_device *s, v4l2_std_id std)
{
	struct tvp5150_decoder *decoder = s->priv;
	int fmt = 0;

	decoder->std_id = std;

	/* First tests should be against specific std */

	if (std == V4L2_STD_NTSC_443) {
		fmt = VIDEO_STD_NTSC_4_43_BIT;
	} else if (std == V4L2_STD_PAL_M) {
		fmt = VIDEO_STD_PAL_M_BIT;
	} else if (std == V4L2_STD_PAL_N || std == V4L2_STD_PAL_Nc) {
		fmt = VIDEO_STD_PAL_COMBINATION_N_BIT;
	} else {
		/* Then, test against generic ones */
		if (std & V4L2_STD_NTSC)
			fmt = VIDEO_STD_NTSC_MJ_BIT;
		else if (std & V4L2_STD_PAL)
			fmt = VIDEO_STD_PAL_BDGHIN_BIT;
		else if (std & V4L2_STD_SECAM)
			fmt = VIDEO_STD_SECAM_BIT;
	}

	dev_dbg(&decoder->i2c_client->dev, "Set video std register to %d.\n", fmt);
	tvp5150_write(s,TVP5150_VIDEO_STD, fmt);
	return 0;
}
#endif

/**
 * ioctl_s_std - V4L2 decoder interface handler for VIDIOC_S_STD ioctl
 * @s: pointer to standard V4L2 device structure
 * @std_id: standard V4L2 v4l2_std_id ioctl enum
 *
 * If std_id is supported, sets the requested standard. Otherwise, returns
 * -EINVAL
 */
static int ioctl_s_std(struct v4l2_int_device *s, v4l2_std_id *std_id)
{
	struct tvp5150_decoder *decoder = s->priv;
	int err, i;

	dev_dbg(&decoder->sen.i2c_client->dev, "In tvp5150:ioctl_s_std\n");

	if (std_id == NULL)
		return -EINVAL;
	if (decoder->std_id == *std_id)
		return 0;

	if (std_id == NULL)
		return -EINVAL;

	for (i = 0; i < decoder->num_stds; i++)
		if (*std_id & decoder->std_list[i].standard.id)
			break;

	if ((i == decoder->num_stds) || (i == STD_INVALID))
		return -EINVAL;

	err = tvp5150_write(s, TVP5150_VIDEO_STD,
			    decoder->std_list[i].video_std_bit);
	if (err)
		return err;

	decoder->current_std = i;
	//decoder->tvp5150_regs[REG_VIDEO_STD].val =
	//	decoder->std_list[i].video_std;

	dev_dbg(&decoder->sen.i2c_client->dev, "Standard set to: %s",
			decoder->std_list[i].standard.name);
	return 0;
	/* Change cropping height limits */
//	if (*std_id & V4L2_STD_525_60)
//		decoder->rect.height = TVP5150_V_MAX_525_60;
//	else
//		decoder->rect.height = TVP5150_V_MAX_OTHERS;
//
//
//	return tvp5150_set_std(s, *std_id);
}

static int tvp5150_reset(struct v4l2_int_device *s)
{
	struct tvp5150_decoder *decoder = s->priv;

	/* Initializes TVP5150 to its default values */
	tvp5150_write_inittab(s,tvp5150_init_default);

	/* Initializes VDP registers */
	//tvp5150_vdp_init(sd, vbi_ram_default);

	/* Selects decoder input */
	tvp5150_selmux(s);

	/* Initializes TVP5150 to stream enabled values */
	tvp5150_write_inittab(s,tvp5150_init_enable);

	/* Initialize image preferences */
	//v4l2_ctrl_handler_setup(&decoder->hdl);

	//tvp5150_set_std(s, decoder->std_id);

	//if (decoder->mbus_type == V4L2_MBUS_PARALLEL)
	//	tvp5150_write(s,sd, TVP5150_DATA_RATE_SEL, 0x40);

	return 0;
};

/***********************************************************************
 * IOCTL Functions from v4l2_int_ioctl_desc.
 ***********************************************************************/

/*!
 * ioctl_g_ifparm - V4L2 sensor interface handler for vidioc_int_g_ifparm_num
 * s: pointer to standard V4L2 device structure
 * p: pointer to standard V4L2 vidioc_int_g_ifparm_num ioctl structure
 *
 * Gets slave interface parameters.
 * Calculates the required xclk value to support the requested
 * clock parameters in p.  This value is returned in the p
 * parameter.
 *
 * vidioc_int_g_ifparm returns platform-specific information about the
 * interface settings used by the sensor.
 *
 * Called on open.
 */
static int ioctl_g_ifparm(struct v4l2_int_device *s, struct v4l2_ifparm *p)
{
	struct tvp5150_decoder *decoder = s->priv;	
	dev_dbg(&decoder->sen.i2c_client->dev, "In tvp5150:ioctl_g_ifparm\n");
//	v4l_dbg(1, debug, decoder->i2c_client, "tvp5150:ioctl_g_ifparm\n");

	if (s == NULL) {
		pr_err("   ERROR!! no slave device set!\n");
		return -1;
	}

	/* Initialize structure to 0s then set any non-0 values. */
	memset(p, 0, sizeof(*p));
	p->if_type = V4L2_IF_TYPE_BT656; // This is the only possibility.
	p->u.bt656.mode = V4L2_IF_TYPE_BT656_MODE_BT_8BIT;
	p->u.bt656.latch_clk_inv = 0;
	p->u.bt656.nobt_vs_inv = 0;
	p->u.bt656.nobt_hs_inv = 1; //may be set it all to 0
	p->u.bt656.bt_sync_correct = 1;
	p->u.bt656.interlace = 1;
	p->u.bt656.clock_curr = 27000000;

	/* tvp5150 has a dedicated clock so no clock settings needed. */

	return 0;
}

/*!
 * Sets the camera power.
 *
 * s  pointer to the camera device
 * on if 1, power is to be turned on.  0 means power is to be turned off
 *
 * ioctl_s_power - V4L2 sensor interface handler for vidioc_int_s_power_num
 * @s: pointer to standard V4L2 device structure
 * @on: power state to which device is to be set
 *
 * Sets devices power state to requrested state, if possible.
 * This is called on open, close, suspend and resume.
 */
static int ioctl_s_power(struct v4l2_int_device *s, int on)
{
	struct tvp5150_decoder *decoder = s->priv;

	dev_dbg(&decoder->sen.i2c_client->dev, "In tvp5150:ioctl_s_power\n");
	//v4l_dbg(1, debug, decoder->i2c_client, "tvp5150:ioctl_s_power\n");

#if 0
	if (on && !decoder->sen.on) {
		if (tvp5150_write(s,tvp5150_PWR_MNG, 0x04) != 0)
			return -EIO;

		/*
		 * FIXME:Additional 400ms to wait the chip to be stable?
		 * This is a workaround for preview scrolling issue.
		 */
		msleep(400);
	} else if (!on && decoder->sen.on) {
		if (tvp5150_write(s,tvp5150_PWR_MNG, 0x24) != 0)
			return -EIO;
	}
#endif

	decoder->sen.on = on;
	return 0;
}

/*!
 * ioctl_g_parm - V4L2 sensor interface handler for VIDIOC_G_PARM ioctl
 * @s: pointer to standard V4L2 device structure
 * @a: pointer to standard V4L2 VIDIOC_G_PARM ioctl structure
 *
 * Returns the decoder's video CAPTURE parameters.
 */
static int ioctl_g_parm(struct v4l2_int_device *s, struct v4l2_streamparm *a)
{
	struct tvp5150_decoder *decoder = s->priv;
	struct v4l2_captureparm *cparm = &a->parm.capture;
	enum tvp5150_std current_std;

	dev_dbg(&decoder->sen.i2c_client->dev, "In tvp5150:ioctl_g_parm\n");
//	v4l_dbg(1, debug, decoder->i2c_client, "In tvp5150:ioctl_g_parm\n");

	if (a == NULL)
		return -EINVAL;

	if (a->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;	/* only capture is supported */

	memset(a, 0, sizeof(*a));
	a->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	/* get the current standard */
	current_std = tvp5150_get_current_std(decoder);
	if (current_std == STD_INVALID)
		return -EINVAL;

	decoder->current_std = current_std;

	cparm->capability = 0/*V4L2_CAP_TIMEPERFRAME*/;		/*  Supported modes */
	cparm->timeperframe =
		decoder->std_list[current_std].standard.frameperiod;
	//cparm->capability = decoder->sen.streamcap.capability;		/*  Supported modes */
	//cparm->timeperframe = decoder->sen.streamcap.timeperframe;	/*  Time per frame in seconds */
	//cparm->capturemode = decoder->sen.streamcap.capturemode;	/*  Current mode */
	//cparm->extendedmode = sensor->sen.streamcap.extendedmode;	/*  Driver-specific extensions */

	return 0;
}

/*!
 * ioctl_s_parm - V4L2 sensor interface handler for VIDIOC_S_PARM ioctl
 * @s: pointer to standard V4L2 device structure
 * @a: pointer to standard V4L2 VIDIOC_S_PARM ioctl structure
 *
 * Configures the sensor to use the input parameters, if possible.  If
 * not possible, reverts to the old parameters and returns the
 * appropriate error code.
 *
  */
static int ioctl_s_parm(struct v4l2_int_device *s, struct v4l2_streamparm *a)
{
	struct tvp5150_decoder *decoder = s->priv;
	struct v4l2_fract *timeperframe;
	enum tvp5150_std current_std;

	dev_dbg(&decoder->sen.i2c_client->dev, "In tvp5150:ioctl_s_parm\n");
//	v4l_dbg(1, debug, decoder->i2c_client, "In tvp5150:ioctl_s_parm\n");

	if (a == NULL)
		return -EINVAL;

	if (a->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;	/* only capture is supported */

	timeperframe = &a->parm.capture.timeperframe;

	/* get the current standard */
	current_std = tvp5150_get_current_std(decoder);
	if (current_std == STD_INVALID)
		return -EINVAL;

	decoder->current_std = current_std;

	*timeperframe =
	    decoder->std_list[current_std].standard.frameperiod;


	return 0;
}
/*!
 * ioctl_queryctrl - V4L2 sensor interface handler for VIDIOC_QUERYCTRL ioctl
 * @s: pointer to standard V4L2 device structure
 * @qc: standard V4L2 VIDIOC_QUERYCTRL ioctl structure
 *
 * If the requested control is supported, returns the control information
 * from the video_control[] array.  Otherwise, returns -EINVAL if the
 * control is not supported.
 */
static int ioctl_queryctrl(struct v4l2_int_device *s,
			   struct v4l2_queryctrl *qc)
{
	struct tvp5150_decoder *decoder = s->priv;
	int i;

	dev_dbg(&decoder->sen.i2c_client->dev, "In tvp5150:ioctl_queryctrl\n");
//	v4l_dbg(1, debug, decoder->i2c_client, "tvp5150:ioctl_queryctrl\n");

	for (i = 0; i < ARRAY_SIZE(tvp5150_qctrl); i++)
		if (qc->id && qc->id == tvp5150_qctrl[i].id) {
			memcpy(qc, &(tvp5150_qctrl[i]),
				sizeof(*qc));
			return 0;
		}

	return -EINVAL;
}

/*!
 * ioctl_g_ctrl - V4L2 sensor interface handler for VIDIOC_G_CTRL ioctl
 * @s: pointer to standard V4L2 device structure
 * @vc: standard V4L2 VIDIOC_G_CTRL ioctl structure
 *
 * If the requested control is supported, returns the control's current
 * value from the video_control[] array.  Otherwise, returns -EINVAL
 * if the control is not supported.
 */
static int ioctl_g_ctrl(struct v4l2_int_device *s, struct v4l2_control *vc)
{
	struct tvp5150_decoder *decoder = s->priv;

	dev_dbg(&decoder->sen.i2c_client->dev, "In tvp5150:ioctl_g_ctrl\n");

	switch (vc->id) {
	case V4L2_CID_BRIGHTNESS:
		vc->value = tvp5150_read(s,TVP5150_BRIGHT_CTL);
		return 0;
	case V4L2_CID_CONTRAST:
		vc->value = tvp5150_read(s,TVP5150_CONTRAST_CTL);
		return 0;
	case V4L2_CID_SATURATION:
		vc->value = tvp5150_read(s,TVP5150_SATURATION_CTL);
		return 0;
	case V4L2_CID_HUE:
		vc->value = tvp5150_read(s,TVP5150_HUE_CTL);
		return 0;
	}
	vc->value = 0;
	return -EINVAL;
}

/*!
 * ioctl_s_ctrl - V4L2 sensor interface handler for VIDIOC_S_CTRL ioctl
 * @s: pointer to standard V4L2 device structure
 * @vc: standard V4L2 VIDIOC_S_CTRL ioctl structure
 *
 * If the requested control is supported, sets the control's current
 * value in HW (and updates the video_control[] array).  Otherwise,
 * returns -EINVAL if the control is not supported.
 */
static int ioctl_s_ctrl(struct v4l2_int_device *s, struct v4l2_control *vc)
{
	struct tvp5150_decoder *decoder = s->priv;

	dev_dbg(&decoder->sen.i2c_client->dev, "In tvp5150:ioctl_s_ctrl\n");

	switch (vc->id) {
	case V4L2_CID_BRIGHTNESS:
		tvp5150_write(s,TVP5150_BRIGHT_CTL, vc->value);
		//decoder->brightness = vc->value;
		return 0;
	case V4L2_CID_CONTRAST:
		tvp5150_write(s,TVP5150_CONTRAST_CTL, vc->value);
		//decoder->contrast = vc->value;
		return 0;
	case V4L2_CID_SATURATION:
		tvp5150_write(s,TVP5150_SATURATION_CTL, vc->value);
		//decoder->saturation = vc->value;
		return 0;
	case V4L2_CID_HUE:
		tvp5150_write(s,TVP5150_HUE_CTL, vc->value);
		//decoder->hue = vc->value;
		return 0;
	}
	return -EINVAL;
}

/*!
 * ioctl_enum_framesizes - V4L2 sensor interface handler for
 *			   VIDIOC_ENUM_FRAMESIZES ioctl
 * @s: pointer to standard V4L2 device structure
 * @fsize: standard V4L2 VIDIOC_ENUM_FRAMESIZES ioctl structure
 *
 * Return 0 if successful, otherwise -EINVAL.
 */
static int ioctl_enum_framesizes(struct v4l2_int_device *s,
				 struct v4l2_frmsizeenum *fsize)
{
	struct tvp5150_decoder *decoder = s->priv;

	dev_dbg(&decoder->sen.i2c_client->dev, "In tvp5150:ioctl_enum_framesizes\n");
	
	if (fsize->index >= 1 /*||
	    fsize->pixel_format != tvp->pix.pixelformat*/)
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	fsize->discrete.width = decoder->sen.pix.width;
	fsize->discrete.height  = decoder->sen.pix.height;

	return 0;
}

/*!
 * ioctl_enum_frameintervals - V4L2 sensor interface handler for
 *			       VIDIOC_ENUM_FRAMEINTERVALS ioctl
 * @s: pointer to standard V4L2 device structure
 * @fival: standard V4L2 VIDIOC_ENUM_FRAMEINTERVALS ioctl structure
 *
 * Return 0 if successful, otherwise -EINVAL.
 */
static int ioctl_enum_frameintervals(struct v4l2_int_device *s,
					 struct v4l2_frmivalenum *fival)
{
	struct tvp5150_decoder *decoder = s->priv;

	dev_dbg(&decoder->sen.i2c_client->dev, "In tvp5150:ioctl_enum_frameintervals\n");

	if (fival->index != 0 /*||
	    fival->pixel_format != tvp->pix.pixelformat*/)
		return -EINVAL;

	if (fival->width  == decoder->sen.pix.width &&
	    fival->height == decoder->sen.pix.height) {
		fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
		fival->discrete.numerator = 1;
		if (decoder->std_id & V4L2_STD_525_60)
			fival->discrete.denominator = 30;
		else
			fival->discrete.denominator = 25;
		return 0;
	}

	return -EINVAL;
}

/*!
 * ioctl_g_chip_ident - V4L2 sensor interface handler for
 *			VIDIOC_DBG_G_CHIP_IDENT ioctl
 * @s: pointer to standard V4L2 device structure
 * @id: pointer to int
 *
 * Return 0.
 */
static int ioctl_g_chip_ident(struct v4l2_int_device *s, int *id)
{
	struct tvp5150_decoder *decoder = s->priv;

	dev_dbg(&decoder->sen.i2c_client->dev, "In tvp5150:ioctl_g_chip_ident\n");

	((struct v4l2_dbg_chip_ident *)id)->match.type =
					V4L2_CHIP_MATCH_I2C_DRIVER;
	strcpy(((struct v4l2_dbg_chip_ident *)id)->match.name,
						"adv_tvp5150_decoder");
//						"tvp5150_decoder");
	((struct v4l2_dbg_chip_ident *)id)->ident = V4L2_IDENT_TVP5150;

	return 0;
}

/*!
 * ioctl_enum_fmt_cap - V4L2 sensor interface handler for VIDIOC_ENUM_FMT
 * @s: pointer to standard V4L2 device structure
 * @fmt: pointer to standard V4L2 fmt description structure
 *
 * Implement the VIDIOC_ENUM_FMT ioctl to enumerate supported formats
 * Return 0.
 */
static int ioctl_enum_fmt_cap(struct v4l2_int_device *s,
			      struct v4l2_fmtdesc *fmt)
{
	struct tvp5150_decoder *decoder = s->priv;
	int index;

	dev_dbg(&decoder->sen.i2c_client->dev, "In tvp5150:ioctl_enum_fmt_cap\n");

	if (fmt == NULL)
		return -EINVAL;

	index = fmt->index;
	if ((index >= decoder->num_fmts) || (index < 0))
		return -EINVAL;	/* Index out of bound */

	if (fmt->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;	/* only capture is supported */

	memcpy(fmt, &decoder->fmt_list[index],
		sizeof(struct v4l2_fmtdesc));

	dev_dbg(&decoder->sen.i2c_client->dev,
			"Current FMT: index - %d (%s)",
			decoder->fmt_list[index].index,
			decoder->fmt_list[index].description);
	return 0;
}

#if 0
static int ioctl_enum_fmt_cap(struct v4l2_int_device *s,
			      struct v4l2_fmtdesc *fmt)
{
	struct tvp5150_decoder *tvp = s->priv;

	if (fmt->index > 0 //||	/* only 1 pixelformat support so far */
		/*fmt->type != V4L2_BUF_TYPE_VIDEO_CAPTURE*/)
		return -EINVAL;
	
	fmt->pixelformat = tvp->pix.pixelformat;
	return 0;
}
#endif

/**
 * ioctl_try_fmt_cap - Implement the CAPTURE buffer VIDIOC_TRY_FMT ioctl
 * @s: pointer to standard V4L2 device structure
 * @f: pointer to standard V4L2 VIDIOC_TRY_FMT ioctl structure
 *
 * Implement the VIDIOC_TRY_FMT ioctl for the CAPTURE buffer type. This
 * ioctl is used to negotiate the image capture size and pixel format
 * without actually making it take effect.
 */
static int
ioctl_try_fmt_cap(struct v4l2_int_device *s, struct v4l2_format *f)
{
	struct tvp5150_decoder *decoder = s->priv;
	int ifmt;
	struct v4l2_pix_format *pix;
	enum tvp5150_std current_std;

	dev_dbg(&decoder->sen.i2c_client->dev, "In tvp5150:ioctl_try_fmt_cap\n");

	if (f == NULL)
		return -EINVAL;

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	pix = &f->fmt.pix;

	/* Calculate height and width based on current standard */
	current_std = tvp5150_get_current_std(decoder);
	if (current_std == STD_INVALID)
		return -EINVAL;

	decoder->current_std = current_std;
	pix->width = decoder->std_list[current_std].width;
	pix->height = decoder->std_list[current_std].height;

	for (ifmt = 0; ifmt < decoder->num_fmts; ifmt++) {
		if (pix->pixelformat ==
			decoder->fmt_list[ifmt].pixelformat)
			break;
	}
	if (ifmt == decoder->num_fmts)
		ifmt = 0;	/* None of the format matched, select default */
	pix->pixelformat = decoder->fmt_list[ifmt].pixelformat;

	pix->field = V4L2_FIELD_INTERLACED;
//	pix->bytesperline = pix->width * 2;
//	pix->sizeimage = pix->bytesperline * pix->height;
	pix->colorspace = V4L2_COLORSPACE_SMPTE170M;
	pix->priv = 1;

	dev_dbg(&decoder->sen.i2c_client->dev,
			"Try FMT: pixelformat - %s, bytesperline - %d"
			"Width - %d, Height - %d",
			decoder->fmt_list[ifmt].description, pix->bytesperline,
			pix->width, pix->height);
	return 0;
}

/**
 * ioctl_s_fmt_cap - V4L2 decoder interface handler for VIDIOC_S_FMT ioctl
 * @s: pointer to standard V4L2 device structure
 * @f: pointer to standard V4L2 VIDIOC_S_FMT ioctl structure
 *
 * If the requested format is supported, configures the HW to use that
 * format, returns error code if format not supported or HW can't be
 * correctly configured.
 */
static int
ioctl_s_fmt_cap(struct v4l2_int_device *s, struct v4l2_format *f)
{
	struct tvp5150_decoder *decoder = s->priv;
	struct v4l2_pix_format *pix;
	int rval;

	dev_dbg(&decoder->sen.i2c_client->dev, "In tvp5150:ioctl_s_fmt_cap\n");

	if (f == NULL)
		return -EINVAL;

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;	/* only capture is supported */

	pix = &f->fmt.pix;
	rval = ioctl_try_fmt_cap(s, f);
	if (rval)
		return rval;

		decoder->sen.pix = *pix;

	return rval;
}

/*!
 * ioctl_g_fmt_cap - V4L2 sensor interface handler for ioctl_g_fmt_cap
 * @s: pointer to standard V4L2 device structure
 * @f: pointer to standard V4L2 v4l2_format structure
 *
 * Returns the sensor's current pixel format in the v4l2_format
 * parameter.
 */
static int ioctl_g_fmt_cap(struct v4l2_int_device *s, struct v4l2_format *f)
{
	struct tvp5150_decoder *decoder = s->priv;
	enum tvp5150_std current_std;

	dev_dbg(&decoder->sen.i2c_client->dev, "In tvp5150:ioctl_g_fmt_cap\n");

	if (f == NULL)
		return -EINVAL;

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;	/* only capture is supported */

	/* Calculate height and width based on current standard */
	current_std = tvp5150_get_current_std(decoder);
	if (current_std == STD_INVALID)
		return -EINVAL;

	decoder->current_std = current_std;
	decoder->sen.pix.width = decoder->std_list[current_std].width;
	decoder->sen.pix.height = decoder->std_list[current_std].height;

	f->fmt.pix = decoder->sen.pix;

	pr_debug("       Current FMT: bytesperline - %d "
			"Width - %d, Height - %d, "
			"Pixelformat - %c%c%c%c",
			decoder->sen.pix.bytesperline,
			decoder->sen.pix.width, decoder->sen.pix.height,
			decoder->sen.pix.pixelformat & 0xff, (decoder->sen.pix.pixelformat >> 8) & 0xff,
			(decoder->sen.pix.pixelformat >> 16) & 0xff, (decoder->sen.pix.pixelformat >> 24) & 0xff);
	return 0;
}


/****************************************************************************
			I2C Command
 ****************************************************************************/

static int ioctl_s_routing(struct v4l2_int_device *s,
			     u32 input, u32 output, u32 config)
{
	struct tvp5150_decoder *decoder = s->priv;

	dev_dbg(&decoder->sen.i2c_client->dev, "In tvp5150:ioctl_s_routing\n");

	decoder->route.input = input;
	decoder->route.output = output;
	tvp5150_selmux(s);
	return 0;
}

static ssize_t tvp5150_input_write(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf,
				 size_t count)
{
	struct tvp5150_decoder *decoder = dev_get_drvdata(dev);
	int in;

	if(*buf == '0') {
		decoder->route.input = TVP5150_COMPOSITE0;
		decoder->route.output = TVP5150_NORMAL;
		tvp5150_selmux(&decoder->v4l2_int_device);
		return count;
	}
	else if (*buf == '1') {
		decoder->route.input = TVP5150_COMPOSITE1;
		decoder->route.output = TVP5150_NORMAL;
		tvp5150_selmux(&decoder->v4l2_int_device);
		return count;
	}
	else
		return -EINVAL;
}

static ssize_t tvp5150_input_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct tvp5150_decoder *decoder = dev_get_drvdata(dev);
	return scnprintf(buf,PAGE_SIZE,"%i\n",
				decoder->route.input);
}

static DEVICE_ATTR(input, S_IRUGO | S_IWUSR, tvp5150_input_show, tvp5150_input_write);
/* ----------------------------------------------------------------------- */

/*!
 * ioctl_init - V4L2 sensor interface handler for VIDIOC_INT_INIT
 * @s: pointer to standard V4L2 device structure
 */
static int ioctl_init(struct v4l2_int_device *s)
{
	struct tvp5150_decoder *decoder = s->priv;

	dev_dbg(&decoder->sen.i2c_client->dev, "In tvp5150:ioctl_init\n");
//	v4l_dbg(1, debug, decoder->i2c_client, "In tvp5150:ioctl_init\n");
	return 0;
}

/*!
 * ioctl_dev_init - V4L2 sensor interface handler for vidioc_int_dev_init_num
 * @s: pointer to standard V4L2 device structure
 *
 * Initialise the device when slave attaches to the master.
 */
static int ioctl_dev_init(struct v4l2_int_device *s)
{
	struct tvp5150_decoder *decoder = s->priv;

	dev_dbg(&decoder->sen.i2c_client->dev, "In tvp5150:ioctl_dev_init\n");
//	v4l_dbg(1, debug, decoder->i2c_client, "tvp5150:ioctl_dev_init\n");
	return 0;
}

/*!
 * This structure defines all the ioctls for this module.
 */
static struct v4l2_int_ioctl_desc tvp5150_ioctl_desc[] = {

	{vidioc_int_dev_init_num, (v4l2_int_ioctl_func*)ioctl_dev_init},

	/*!
	 * Delinitialise the dev. at slave detach.
	 * The complement of ioctl_dev_init.
	 */
	{vidioc_int_s_power_num, (v4l2_int_ioctl_func*)ioctl_s_power},
	{vidioc_int_g_ifparm_num, (v4l2_int_ioctl_func*)ioctl_g_ifparm},
	{vidioc_int_init_num, (v4l2_int_ioctl_func*)ioctl_init},

	/*!
	 * VIDIOC_ENUM_FMT ioctl for the CAPTURE buffer type.
	 */
	{vidioc_int_enum_fmt_cap_num,
				(v4l2_int_ioctl_func *)ioctl_enum_fmt_cap}, 
	{vidioc_int_try_fmt_cap_num,
	 			(v4l2_int_ioctl_func *) ioctl_try_fmt_cap},

	/*!
	 * VIDIOC_TRY_FMT ioctl for the CAPTURE buffer type.
	 * This ioctl is used to negotiate the image capture size and
	 * pixel format without actually making it take effect.
	 */

	{vidioc_int_g_fmt_cap_num, (v4l2_int_ioctl_func*)ioctl_g_fmt_cap},
	{vidioc_int_s_fmt_cap_num,
	 			(v4l2_int_ioctl_func *) ioctl_s_fmt_cap},

	/*!
	 * If the requested format is supported, configures the HW to use that
	 * format, returns error code if format not supported or HW can't be
	 * correctly configured.
	 */

	{vidioc_int_g_parm_num, (v4l2_int_ioctl_func*)ioctl_g_parm},
	{vidioc_int_s_parm_num, (v4l2_int_ioctl_func*)ioctl_s_parm},
	{vidioc_int_queryctrl_num, (v4l2_int_ioctl_func*)ioctl_queryctrl},
	{vidioc_int_g_ctrl_num, (v4l2_int_ioctl_func*)ioctl_g_ctrl},
	{vidioc_int_s_ctrl_num, (v4l2_int_ioctl_func*)ioctl_s_ctrl},
	{vidioc_int_querystd_num, (v4l2_int_ioctl_func *) ioctl_querystd},
//	{vidioc_int_s_std_num, (v4l2_int_ioctl_func *) ioctl_s_std},
	{vidioc_int_enum_framesizes_num,
				(v4l2_int_ioctl_func *)ioctl_enum_framesizes},
	{vidioc_int_enum_frameintervals_num,
				(v4l2_int_ioctl_func *)
				ioctl_enum_frameintervals},
	{vidioc_int_g_chip_ident_num,
				(v4l2_int_ioctl_func *)ioctl_g_chip_ident},
	{vidioc_int_s_video_routing_num,
				(v4l2_int_ioctl_func *) ioctl_s_routing},
};

static struct tvp5150_decoder tvp5150_data = {
//	.state = STATE_NOT_DETECTED,

	.fmt_list = tvp5150_fmt_list,
	.num_fmts = ARRAY_SIZE(tvp5150_fmt_list),

	.sen = {
		.pix = {		/* Default to NTSC 8-bit YUV 422 */
			.width = TVP5150_H_MAX,
			.height = TVP5150_V_MAX_525_60,
			.pixelformat = V4L2_PIX_FMT_UYVY,
			.field = V4L2_FIELD_INTERLACED,// V4L2_FIELD_ALTERNATE,
			.bytesperline = TVP5150_H_MAX * 2,
			.sizeimage =
				TVP5150_H_MAX * 2 * TVP5150_V_MAX_525_60,
			.colorspace = V4L2_COLORSPACE_SMPTE170M,
			.priv = 1,  /* 1 is used to indicate TV in */
			},
		.on = true,
	},

	.current_std = STD_NTSC_MJ,
	.std_list = tvp5150_std_list,
	.num_stds = ARRAY_SIZE(tvp5150_std_list),
	.v4l2_int_device = {
		.module = THIS_MODULE,
		.name = TVP5150_MODULE_NAME,
		.type = v4l2_int_type_slave,
	},
	.tvp5150_slave = {
		.ioctls = tvp5150_ioctl_desc,
		.num_ioctls = ARRAY_SIZE(tvp5150_ioctl_desc),
	},
};

static struct attribute *tvp5150_attributes[] = {
	&dev_attr_reg.attr,
	&dev_attr_input.attr,
	NULL,
};

static struct attribute_group tvp5150_attr_group = {
        .attrs = tvp5150_attributes,
};


/***********************************************************************
 * I2C client and driver.
 ***********************************************************************/


static int tvp5150_detect_version(struct tvp5150_decoder *core)
{
	//struct v4l2_subdev *sd = &core->sd;
	//struct i2c_client *c = v4l2_get_subdevdata(sd);
	struct i2c_client *c = core->sen.i2c_client;
	struct v4l2_int_device *s = &core->v4l2_int_device;
	unsigned int i;
	u8 regs[4];
	int res;

	/*
	 * Read consequent registers - TVP5150_MSB_DEV_ID, TVP5150_LSB_DEV_ID,
	 * TVP5150_ROM_MAJOR_VER, TVP5150_ROM_MINOR_VER
	 */
	for (i = 0; i < 4; i++) {
		res = tvp5150_read(s,TVP5150_MSB_DEV_ID + i);
		if (res < 0)
			return res;
		regs[i] = res;
	}

	core->dev_id = (regs[0] << 8) | regs[1];
	core->rom_ver = (regs[2] << 8) | regs[3];

	v4l_info(c, "tvp%04x (%u.%u) chip found @ 0x%02x (%s)\n",
		  core->dev_id, regs[2], regs[3], c->addr << 1,
		  c->adapter->name);

	if (core->dev_id == 0x5150 && core->rom_ver == 0x0321) {
		v4l_info(c, "tvp5150a detected.\n");
	} else if (core->dev_id == 0x5150 && core->rom_ver == 0x0400) {
		v4l_info(c, "tvp5150am1 detected.\n");

		/* ITU-T BT.656.4 timing */
		tvp5150_write(s,TVP5150_REV_SELECT, 0);
	} else if (core->dev_id == 0x5151 && core->rom_ver == 0x0100) {
		v4l_info(c, "tvp5151 detected.\n");
	} else {
		v4l_info(c, "*** unknown tvp%04x chip detected.\n",
			  core->dev_id);
	}

	return 0;
}

static int tvp5150_init(struct i2c_client *c)
{
	struct gpio_desc *pdn_gpio;
	struct gpio_desc *reset_gpio;

	pdn_gpio = devm_gpiod_get_optional(&c->dev, "pdn", GPIOD_OUT_HIGH);
	if (IS_ERR(pdn_gpio))
		return PTR_ERR(pdn_gpio);

	if (pdn_gpio) {
		gpiod_set_value_cansleep(pdn_gpio, 0);
		/* Delay time between power supplies active and reset */
		msleep(20);
	}

	reset_gpio = devm_gpiod_get_optional(&c->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(reset_gpio))
		return PTR_ERR(reset_gpio);

	if (reset_gpio) {
		/* RESETB pulse duration */
		ndelay(500);
		gpiod_set_value_cansleep(reset_gpio, 0);
		/* Delay time between end of reset to I2C active */
		usleep_range(200, 250);
	}

	return 0;
}

/*! tvp5150 Reset function. НЕ ТУТ
 *
 *  @return		None.
 */
#if 0
//static void tvp5150_hard_reset(bool cvbs)
//{
//	dev_dbg(&tvp5150_data.i2c_client->dev,
//		"In tvp5150:tvp5150_hard_reset\n");

	tvp5150_write(s,0x06, 0x70);
	msleep(100);
	tvp5150_write(s,0x03, 0x09);
	msleep(100);

	/*pr_info("tvp5150: adr 0x03 val 0x%X \n", tvp5150_read(s,0x03));
	pr_info("tvp5150: adr 0x06 val 0x%X \n", tvp5150_read(s,0x06));	*/
}
#endif

/*! tvp5150 I2C attach function.
 *
 *  @param *adapter	struct i2c_adapter *.
 *
 *  @return		Error code indicating success or failure.
 */

/*!
 * tvp5150 I2C probe function.
 * Function set in i2c_driver struct.
 * Called by insmod.
 *
 *  @param *adapter	I2C adapter descriptor.
 *
 *  @return		Error code indicating success or failure.
 */
static int tvp5150_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct tvp5150_decoder *decoder;
	int ret = 0;
	struct pinctrl *pinctrl;
	struct device *dev = &client->dev;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter,
	     I2C_FUNC_SMBUS_READ_BYTE | I2C_FUNC_SMBUS_WRITE_BYTE_DATA))
		return -EIO;

	decoder = kzalloc(sizeof(*decoder), GFP_KERNEL);
	if (!decoder)
		return -ENOMEM;

//	if (!client->dev.platform_data) {
//		v4l_err(client, "No platform data!!\n");
//		ret = -ENODEV;
//		goto out_free;
//	}

	*decoder = tvp5150_data;
	decoder->v4l2_int_device.priv = decoder;
	decoder->sen.v4l2_int_device = &decoder->v4l2_int_device;
	decoder->v4l2_int_device.u.slave = &decoder->tvp5150_slave;

	/*
	 * Save the id data, required for power up sequence
	 */
	decoder->id = (struct i2c_device_id *)id;
	/* Attach to Master */
//	strcpy(decoder->v4l2_int_device.u.slave->attach_to,
//			decoder->pdata->master);
	decoder->sen.i2c_client = client;
	i2c_set_clientdata(client, decoder);


	
	ret = tvp5150_init(client);
	if (ret)
		return ret;

	/* pinctrl */
	pinctrl = devm_pinctrl_get_select_default(dev);
	if (IS_ERR(pinctrl)) {
		dev_err(dev, "tvp5150 setup pinctrl failed!");
		return PTR_ERR(pinctrl);
	}


	msleep(1);

	/* Set initial values for the sensor struct. */
//	memset(&tvp5150_data, 0, sizeof(tvp5150_data));
//	tvp5150_data.sen.streamcap.capability = V4L2_CAP_TIMEPERFRAME;
//	tvp5150_data.sen.streamcap.capturemode = 0;
//	tvp5150_data.sen.streamcap.timeperframe.denominator = 30;
//	tvp5150_data.sen.streamcap.timeperframe.numerator = 1;
//	tvp5150_data.std_id = V4L2_STD_ALL;
//	tvp5150_data.pix.width = TVP5150_H_MAX;
//	tvp5150_data.pix.height = TVP5150_V_MAX_OTHERS;
//	tvp5150_data.pix.pixelformat = V4L2_PIX_FMT_UYVY;  /* YUV422 */
//	tvp5150_data.pix.field = V4L2_FIELD_ALTERNATE;  /* YUV422 */
//	tvp5150_data.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;  /* YUV422 */
//	tvp5150_data.pix.priv = 1;  /* 1 is used to indicate TV in */
//	tvp5150_data.sen.on = true;

	decoder->sen.sensor_clk = devm_clk_get(dev, "csi_mclk");
	if (IS_ERR(decoder->sen.sensor_clk)) {
		dev_err(dev, "get mclk failed\n");
		return PTR_ERR(decoder->sen.sensor_clk);
	}

	ret = of_property_read_u32(dev->of_node, "mclk",
					&decoder->sen.mclk);
	if (ret) {
		dev_err(dev, "mclk frequency is invalid\n");
		return ret;
	}

	ret = of_property_read_u32(
		dev->of_node, "mclk_source",
		(u32 *) &(decoder->sen.mclk_source));
	if (ret) {
		dev_err(dev, "mclk_source invalid\n");
		return ret;
	}
	
	ret = of_property_read_u32(dev->of_node, "csi_id",
					&(decoder->sen.csi));
	if (ret) {
		dev_err(dev, "csi_id invalid\n");
		return ret;
	}
	

	/* Set initial values for the sensor struct. */
#if 0
	memset(&tvp5150_data, 0, sizeof(tvp5150_data));
	tvp5150_data.sen.streamcap.timeperframe.denominator = 30;
	tvp5150_data.sen.streamcap.timeperframe.numerator = 1;
	//tvp5150_data.std_id = V4L2_STD_ALL;
	//video_idx = TVP5150_NOT_LOCKED;
	tvp5150_data.std_id = V4L2_STD_PAL;
	video_idx = TVP5150_PAL;
	tvp5150_data.pix.width = video_fmts[video_idx].raw_width;
	tvp5150_data.pix.height = video_fmts[video_idx].raw_height;
	tvp5150_data.pix.pixelformat = V4L2_PIX_FMT_UYVY;  /* YUV422 */
	tvp5150_data.pix.priv = 1;  /* 1 is used to indicate TV in */
	tvp5150_data.sen.on = true;
#endif

	clk_prepare_enable(decoder->sen.sensor_clk);

	/*! tvp5150 initialization. */
	//msleep(100);
	//tvp5150_hard_reset();
	//msleep(100);

	ret = tvp5150_detect_version(decoder);
	if (ret < 0)
		return ret;

	decoder->std_id = V4L2_STD_ALL;	/* Default is autodetect */
	decoder->route.input = TVP5150_COMPOSITE0;
	decoder->enable = 1;

	/* Default is no cropping */
	decoder->rect.top = 0;
//	if (tvp5150_read_std(&decoder->v4l2_int_device) & V4L2_STD_525_60)
//		decoder->rect.height = TVP5150_V_MAX_525_60;
//	else
//		decoder->rect.height = TVP5150_V_MAX_OTHERS;
	decoder->rect.left = 0;
//	decoder->rect.width = TVP5150_H_MAX;

	tvp5150_reset(&decoder->v4l2_int_device);
	/* Register with V4L2 layer as slave device */
	/* This function attaches this structure to the /dev/video0 device.
	 * The pointer in priv points to the tvp5150_data structure here.*/
	ret = v4l2_int_device_register(&decoder->v4l2_int_device);
	if (ret) {
		i2c_set_clientdata(client, NULL);
		v4l_err(client,
			"Unable to register to v4l2. Err[%d]\n", ret);
		goto out_free;

	} /*else
		v4l_info(client, "Registered to v4l2 master %s!!\n",
				decoder->pdata->master);*/

	clk_disable_unprepare(decoder->sen.sensor_clk);

	ret = sysfs_create_group(&client->dev.kobj, &tvp5150_attr_group);
	if (ret)
		printk(KERN_ERR "TVP5150: can't create fs group");

	if (debug > 1)
		tvp5150_log_status(&decoder->v4l2_int_device);

	return ret;
out_free:
	kfree(decoder);
	return ret;
}

/*!
 * tvp5150 I2C detach function.
 * Called on rmmod.
 *
 *  @param *client	struct i2c_client*.
 *
 *  @return		Error code indicating success or failure.
 */
static int tvp5150_remove(struct i2c_client *client)
{
	struct tvp5150_decoder *decoder = i2c_get_clientdata(client);	
	/* Power down via i2c */
	//tvp5150_write(s,tvp5150_PWR_MNG, 0x24);

	if(!client->adapter)
		return -ENODEV;
	v4l2_int_device_unregister(&decoder->v4l2_int_device);
	i2c_set_clientdata(client, NULL);

	v4l_dbg(1, debug, client,
		"tvp5150.c: removing tvp5150 adapter on address 0x%x\n",
		client->addr << 1);

	return 0;
}
#if 0
/*!
 * tvp5150 init function.
 * Called on insmod.
 *
 * @return    Error code indicating success or failure.
 */
static __init int tvp5150_init(void)
{
	u8 err = 0;

	pr_debug("In tvp5150_init\n");

	/* Tells the i2c driver what functions to call for this driver. */
	err = i2c_add_driver(&tvp5150_i2c_driver);
	if (err != 0)
		pr_err("%s:driver registration failed, error=%d\n",
			__func__, err);

	return err;
}

/*!
 * tvp5150 cleanup function.
 * Called on rmmod.
 *
 * @return   Error code indicating success or failure.
 */
static void __exit tvp5150_clean(void)
{
	dev_dbg(&tvp5150_data.i2c_client->dev, "In tvp5150_clean\n");
	i2c_del_driver(&tvp5150_i2c_driver);
}

module_init(tvp5150_init);
module_exit(tvp5150_clean);
#endif

/* ----------------------------------------------------------------------- */

static const struct i2c_device_id tvp5150_id[] = {
	{ "tvp5150", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tvp5150_id);

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id tvp5150_of_match[] = {
	{ .compatible = "ti,tvp5150", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, tvp5150_of_match);
#endif

static struct i2c_driver tvp5150_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(tvp5150_of_match),
		.name	= TVP5150_MODULE_NAME,
	},
	.probe		= tvp5150_probe,
	.remove		= tvp5150_remove,
	.id_table	= tvp5150_id,
};

module_i2c_driver(tvp5150_driver);

MODULE_AUTHOR("NPF ATI");
MODULE_DESCRIPTION("Texas Instruments TVP5150A video decoder driver");
MODULE_LICENSE("GPL");
