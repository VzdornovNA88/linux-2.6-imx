/*
 * Driver for Pixcir I2C touchscreen controllers.
 *
 * Copyright (C) 2010-2011 Pixcir, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_device.h>

#define	 MAX_12BIT		((1 << 12) - 1)

#define DRIVENO	21
#define SENSENO	28
#define ENABLE_INT		1	/* 0->Polling, 1->Interupt, 2->Hybrid */
#define EdgeDisable		1	/* if Edge Disable, set it to 1, else reset to 0, OR  SSD2533 set 0 */
#define RunningAverageMode	2	/* {0,8},{5,3},{6,2},{7,1} */
#define RunningAverageDist	4	/* Threshold Between two consecutive points */
#define MicroTimeTInterupt	25000000/* 100Hz - 10,000,000us */
#define FINGERNO		2 /*10*/
#define THRESHOLD 		0x20

#define SSDS53X_SCREEN_MAX_X    639
#define SSDS53X_SCREEN_MAX_Y    479

#define SSD253x_TOUCH_KEY
#undef SSD253x_TOUCH_KEY

#define SSD253x_CUT_EDGE    /* 0x8b must be 0x00;  EdgeDisable set 0 */
#undef  SSD253x_CUT_EDGE
#ifdef SSD253x_CUT_EDGE
		#define XPOS_MAX (DRIVENO -EdgeDisable) *64
		#define YPOS_MAX (SENSENO -EdgeDisable) *64
#endif

#define SSD253x_SIMULATED_KEY
#undef SSD253x_SIMULATED_KEY

#define int2byte(i)	(i>>8)&0x00ff  ,i&0x00ff 
/* Команды */
#define SSD2533_NOP                             0   /* No operation */
#define SSD2533_NOP_size                        1
#define SSD2533_DEVICE_ID                       2   /* Read Device ID */
#define SSD2533_DEVICE_ID_size                  2
#define SSD2533_VERSION_ID                      3   /* Read Version ID */
#define SSD2533_VERSION_ID_size                 2
#define SSD2533_WAKE_UP                         4   /*  */
#define SSD2533_WAKE_UP_size                    1
/* Drive Line Number Register */
#define SSD2533_DRIVE_LINE_NUMBER_cmd           0x06
#define SSD2533_DRIVE_LINE_NUMBER_size          1
/* Sense Line Number Register */
#define SSD2533_SENSE_LINE_NUMBER_cmd           0x07
#define SSD2533_SENSE_LINE_NUMBER_size          1
/* Select Drive Pin for Drive Line */
#define SSD2533_DRIVE_LINE0                     0x08
#define SSD2533_DRIVE_LINE1                     0x09
#define SSD2533_DRIVE_LINE2                     0x0A
#define SSD2533_DRIVE_LINE3                     0x0B
#define SSD2533_DRIVE_LINE4                     0x0C
#define SSD2533_DRIVE_LINE5                     0x0D
#define SSD2533_DRIVE_LINE6                     0x0E
#define SSD2533_DRIVE_LINE7                     0x0F
#define SSD2533_DRIVE_LINE8                     0x10
#define SSD2533_DRIVE_LINE9                     0x11
#define SSD2533_DRIVE_LINE10                    0x12
#define SSD2533_DRIVE_LINE11                    0x13
#define SSD2533_DRIVE_LINE12                    0x14
#define SSD2533_DRIVE_LINE13                    0x15
#define SSD2533_DRIVE_LINE14                    0x16
#define SSD2533_DRIVE_LINE15                    0x17
#define SSD2533_DRIVE_LINE16                    0x18
#define SSD2533_DRIVE_LINE17                    0x19
#define SSD2533_DRIVE_LINE18                    0x1A
#define SSD2533_DRIVE_LINE19                    0x1B
#define SSD2533_DRIVE_LINE20                    0x1C
#define SSD2533_DRIVE_LINE21                    0x1D
#define SSD2533_DRIVE_LINE22                    0x1E
#define SSD2533_DRIVE_LINE_size                 2
#define SSD2533_DRIVE_LINE_LEFT                 0
#define SSD2533_DRIVE_LINE_RIGHT                1
/* Set Operating Mode (Frame scan period in millisecond 0-255) */
#define SSD2533_WOP_MODE                        0x25
#define SSD2533_WOP_MODE_size                   1
/* Read Operating Mode */
#define SSD2533_ROP_MODE                        0x26
#define SSD2533_ROP_MODE_size                   1
/* sub-frame per frame */
#define SSD2533_SCAN_FRAME                      0x2A
#define SSD2533_SCAN_FRAME_size                 1
/* Median Filter Setting */
#define SSD2533_MEDIAN_FILTER_SEL               0x2C
#define SSD2533_MEDIAN_FILTER_SEL_size          1
/* Set integration gain */
#define SSD2533_INT_GAIN                        0x2F
#define SSD2533_INT_GAIN_size                   1
/* Start Time of Integration Window */
#define SSD2533_START_INT                       0x30
#define SSD2533_START_INT_size                  1
/* End Time of Integration Window */
#define SSD2533_END_INT                         0x31
#define SSD2533_END_INT_size                    1
/* Define Min. Finger Area */
#define SSD2533_MIN_AREA                        0x33
#define SSD2533_MIN_AREA_size                   2
/* Define Min. Finger Level */
#define SSD2533_MIN_LEVEL                       0x34
#define SSD2533_MIN_LEVEL_size                  2
/* Define Min. Finger Weight */
#define SSD2533_MIN_WEIGHT                      0x35
#define SSD2533_MIN_WEIGHT_size                 2
/* Define Max. Finger Area */
#define SSD2533_MAX_AREA                        0x36
#define SSD2533_MAX_AREA_size                   2
/* Slicing depth in image segmentation process */
#define SSD2533_SEG_DEPTH                       0x37
#define SSD2533_SEG_DEPTH_size                  1
#define SSD2533_SEG_DEPTH_0_41                  7
#define SSD2533_SEG_DEPTH_0_49                  6
#define SSD2533_SEG_DEPTH_0_56                  5
#define SSD2533_SEG_DEPTH_0_63                  4
#define SSD2533_SEG_DEPTH_0_68                  3
#define SSD2533_SEG_DEPTH_0_73                  2
#define SSD2533_SEG_DEPTH_0_78                  1
#define SSD2533_SEG_DEPTH_0_82                  0
/* Select CG calculation metod */
#define SSD2533_CG_METOD                       	0x39
#define SSD2533_CG_METOD_size                  	1
#define SSD2533_CG_METOD_Weighted_average       0
#define SSD2533_CG_METOD_Curve_fitting		1
#define SSD2533_CG_METOD_Hybrid			2
/* Select 2D filter parameter for delta data smoothing */
#define SSD2533_FILTER_SEL                      0x3D
#define SSD2533_FILTER_SEL_size                 1
#define SSD2533_FILTER_SEL_1_6_1                0
#define SSD2533_FILTER_SEL_1_2_1                1
#define SSD2533_FILTER_SEL_no                   2
/* MOVE tolerance */
#define SSD2533_EVENT_MOVE_TOL                  0x53
#define SSD2533_EVENT_MOVE_TOL_size             1
/* X Tracking Tolerance */
#define SSD2533_X_TRACKING_TOL                  0x54
#define SSD2533_X_TRACKING_TOL_size             2
/* Y Tracking Tolerance */
#define SSD2533_Y_TRACKING_TOL                  0x55
#define SSD2533_Y_TRACKING_TOL_size             2
/* Enable Adaptive Moving Average filter to smooth fingers' output coordinates */
#define SSD2533_MOV_AVG_FILTER                  0x56
#define SSD2533_MOV_AVG_FILTER_size             1
#define SSD2533_MOV_AVG_FILTER_DISABLE          0x00
#define SSD2533_MOV_AVG_FILTER_5_3              0x01
#define SSD2533_MOV_AVG_FILTER_6_2              0x02
#define SSD2533_MOV_AVG_FILTER_7_1              0x03
/* Set orientation */
#define SSD2533_ORIENTATION                     0x65
#define SSD2533_ORIENTATION_size                1
#define SSD2533_ORIENTATION_Normal              0
#define SSD2533_ORIENTATION_Y_Invert            1
#define SSD2533_ORIENTATION_X_Invert            2
#define SSD2533_ORIENTATION_XY_Invert           3
#define SSD2533_ORIENTATION_Tranponse           4
#define SSD2533_ORIENTATION_Tranponse_Y_Invert  5
#define SSD2533_ORIENTATION_Tranponse_X_Invert  6
#define SSD2533_ORIENTATION_Tranponse_XY_Invert 7
/* Scaling factor for X coordinate */
#define SSD2533_X_SCALING                       0x66
#define SSD2533_X_SCALING_size                  2
/* Scaling factor for Y coordinate */
#define SSD2533_Y_SCALING                       0x67
#define SSD2533_Y_SCALING_size                  2
/* Offset in X direction */
#define SSD2533_X_OFFSET                        0x68
#define SSD2533_X_OFFSET_size                   1
/* Offset in Y direction */
#define SSD2533_Y_OFFSET                        0x69
#define SSD2533_Y_OFFSET_size                   1
/* Event mask */
#define SSD2533_TOUCH_STATUS                    0x79
#define SSD2533_TOUCH_STATUS_size               2
/* Event mask */
#define SSD2533_EVENT_MSK                       0x7A
#define SSD2533_EVENT_MSK_size                  2
#define SSD2533_EVENT_MSK_UNKNOWN               0x0080
#define SSD2533_EVENT_MSK_MUST_BE_1             0xC700
#define SSD2533_EVENT_MSK_FL                    0x2000
#define SSD2533_EVENT_MSK_FM                    0x1000
#define SSD2533_EVENT_MSK_FE                    0x0800
/* IRQ mask */
#define SSD2533_IRQ_MSK                         0x7B
#define SSD2533_IRQ_MSK_size                    2
#define SSD2533_IRQ_MSK_FINGER09                0x0020
#define SSD2533_IRQ_MSK_FINGER08                0x0010
#define SSD2533_IRQ_MSK_FINGER07                0x0008
#define SSD2533_IRQ_MSK_FINGER06                0x0004
#define SSD2533_IRQ_MSK_FINGER05                0x0002
#define SSD2533_IRQ_MSK_FINGER04                0x0001
#define SSD2533_IRQ_MSK_FINGER03                0x8000
#define SSD2533_IRQ_MSK_FINGER02                0x4000
#define SSD2533_IRQ_MSK_FINGER01                0x2000
#define SSD2533_IRQ_MSK_FINGER00                0x1000
#define SSD2533_IRQ_MSK_ABNORMAL                0x0800
#define SSD2533_IRQ_MSK_LARGE_OBJ               0x0400
#define SSD2533_IRQ_MSK_FIFO_OVF                0x0200
#define SSD2533_IRQ_MSK_FIFO_DATA_VALID         0x0100
/*Read one event from the Event Stack*/
#define SSD2533_EVENT_STACK                     0x86
#define SSD2533_EVENT_STACK_size                5
/* Clear Event Stack*/
#define SSD2533_EVENT_FIFO_SCLR                 0x87
#define SSD2533_EVENT_FIFO_SCLR_size            1
/* Select Driving voltage */
#define SSD2533_DRIVE_LEVEL                     0xD5
#define SSD2533_DRIVE_LEVEL_size                1
#define SSD2533_DRIVE_LEVEL_5_5V                0
#define SSD2533_DRIVE_LEVEL_6_0V                1
#define SSD2533_DRIVE_LEVEL_6_5V                2
#define SSD2533_DRIVE_LEVEL_7_0V                3
#define SSD2533_DRIVE_LEVEL_7_5V                4
#define SSD2533_DRIVE_LEVEL_8_0V                5
#define SSD2533_DRIVE_LEVEL_8_5V                6
#define SSD2533_DRIVE_LEVEL_9_0V                7
/* ADC Vref range */
#define SSD2533_ADC_RANGE                       0xD7
#define SSD2533_ADC_RANGE_size                  1
#define SSD2533_ADC_RANGE_0_35V                 0
#define SSD2533_ADC_RANGE_0_40V                 1
#define SSD2533_ADC_RANGE_0_45V                 2
#define SSD2533_ADC_RANGE_0_50V                 3
#define SSD2533_ADC_RANGE_0_60V                 4
#define SSD2533_ADC_RANGE_0_70V                 5
#define SSD2533_ADC_RANGE_0_80V                 6
#define SSD2533_ADC_RANGE_0_90V                 7
/* Sense line biasing resistance */
#define SSD2533_BIAS_RES                        0xD8
#define SSD2533_BIAS_RES_size                   1
#define SSD2533_BIAS_RES_7_0k                   0
#define SSD2533_BIAS_RES_7_8k                   1
#define SSD2533_BIAS_RES_8_7k                   2
#define SSD2533_BIAS_RES_9_7k                   3
#define SSD2533_BIAS_RES_10_8k                  4
#define SSD2533_BIAS_RES_12_1k                  5
#define SSD2533_BIAS_RES_13_5k                  6
#define SSD2533_BIAS_RES_15_0k                  7
/* Integrator cap value */
#define SSD2533_INTG_CAP                        0xDB
#define SSD2533_INTG_CAP_size                   1
#define SSD2533_INTG_CAP_none                   0
#define SSD2533_INTG_CAP___0                    1
#define SSD2533_INTG_CAP__1_                    2
#define SSD2533_INTG_CAP__10                    3
#define SSD2533_INTG_CAP_2__                    4
#define SSD2533_INTG_CAP_2_0                    5
#define SSD2533_INTG_CAP_21_                    6
#define SSD2533_INTG_CAP_210                    7

#define IRQ_TO_GPIO_OFFSET			160

#define FINGERNO_MAX	10
#define EVENT_STATUS    121
#define FINGER01_REG    124
#define EVENT_FIFO_SCLR 135









struct i2c_client *client_glob;
struct ssd2533_i2c_ts_data *tsdata_glob;

struct ChipSetting {
	char No;
	char Reg;
	char Data1;
	char Data2;
};

// SSD2533 Setting
// Touch Panel Example
struct ChipSetting ssd253xcfgTable[]={							
  /* Set number of Drive lines  = 21 */
  { SSD2533_DRIVE_LINE_NUMBER_size, 	SSD2533_DRIVE_LINE_NUMBER_cmd, 	DRIVENO-1/*, 0X00*/},	
  /* Set number of Sense lines  = 28 */
  {SSD2533_SENSE_LINE_NUMBER_size, 	SSD2533_SENSE_LINE_NUMBER_cmd, 	SENSENO-1/*, 0X00*/},
  
  /* Select Drive Pin for Drive Line */
  {SSD2533_DRIVE_LINE_size, SSD2533_DRIVE_LINE0,  SSD2533_DRIVE_LINE_LEFT, 0x8A},
  {SSD2533_DRIVE_LINE_size, SSD2533_DRIVE_LINE1,  SSD2533_DRIVE_LINE_LEFT, 0x89},
  {SSD2533_DRIVE_LINE_size, SSD2533_DRIVE_LINE2,  SSD2533_DRIVE_LINE_LEFT, 0x88},
  {SSD2533_DRIVE_LINE_size, SSD2533_DRIVE_LINE3,  SSD2533_DRIVE_LINE_LEFT, 0x87},
  {SSD2533_DRIVE_LINE_size, SSD2533_DRIVE_LINE4,  SSD2533_DRIVE_LINE_LEFT, 0x86},
  {SSD2533_DRIVE_LINE_size, SSD2533_DRIVE_LINE5,  SSD2533_DRIVE_LINE_LEFT, 0x85},
  {SSD2533_DRIVE_LINE_size, SSD2533_DRIVE_LINE6,  SSD2533_DRIVE_LINE_LEFT, 0x84},
  {SSD2533_DRIVE_LINE_size, SSD2533_DRIVE_LINE7,  SSD2533_DRIVE_LINE_LEFT, 0x83},
  {SSD2533_DRIVE_LINE_size, SSD2533_DRIVE_LINE8,  SSD2533_DRIVE_LINE_LEFT, 0x82},
  {SSD2533_DRIVE_LINE_size, SSD2533_DRIVE_LINE9,  SSD2533_DRIVE_LINE_LEFT, 0x81},
  {SSD2533_DRIVE_LINE_size, SSD2533_DRIVE_LINE10, SSD2533_DRIVE_LINE_LEFT, 0x80},
  {SSD2533_DRIVE_LINE_size, SSD2533_DRIVE_LINE11, SSD2533_DRIVE_LINE_LEFT, 0x8B},
  {SSD2533_DRIVE_LINE_size, SSD2533_DRIVE_LINE12, SSD2533_DRIVE_LINE_LEFT, 0x8C},
  {SSD2533_DRIVE_LINE_size, SSD2533_DRIVE_LINE13, SSD2533_DRIVE_LINE_LEFT, 0x8D},
  {SSD2533_DRIVE_LINE_size, SSD2533_DRIVE_LINE14, SSD2533_DRIVE_LINE_LEFT, 0x8E},
  {SSD2533_DRIVE_LINE_size, SSD2533_DRIVE_LINE15, SSD2533_DRIVE_LINE_LEFT, 0x8F},
  {SSD2533_DRIVE_LINE_size, SSD2533_DRIVE_LINE16, SSD2533_DRIVE_LINE_LEFT, 0x90},
  {SSD2533_DRIVE_LINE_size, SSD2533_DRIVE_LINE17, SSD2533_DRIVE_LINE_LEFT, 0x91},
  {SSD2533_DRIVE_LINE_size, SSD2533_DRIVE_LINE18, SSD2533_DRIVE_LINE_LEFT, 0x92},
  {SSD2533_DRIVE_LINE_size, SSD2533_DRIVE_LINE19, SSD2533_DRIVE_LINE_LEFT, 0x93},
  {SSD2533_DRIVE_LINE_size, SSD2533_DRIVE_LINE20, SSD2533_DRIVE_LINE_LEFT, 0x94},

  /* Set driving voltage  = 7V */
  {SSD2533_DRIVE_LEVEL_size, SSD2533_DRIVE_LEVEL, SSD2533_DRIVE_LEVEL_8_0V},
  /* Sense line biasing resistance  = 15 kOm */
  {SSD2533_BIAS_RES_size, SSD2533_BIAS_RES, SSD2533_BIAS_RES_10_8k},
			
  /* Drive Pulse */
  {1, 0x2E, 0x0B},

  /* Define Min. Finger Area (0-255) = 1 */
  {SSD2533_MIN_AREA_size, SSD2533_MIN_AREA, int2byte(1)},
  /* Define Min. Finger Level (0-511) = 80 */
  {SSD2533_MIN_LEVEL_size, SSD2533_MIN_LEVEL, int2byte(21)},	//{ 2, 0X34, 0X00, 0x64},
  /* Define Min. Finger Weight (0-65535) = 0 */
  {SSD2533_MIN_WEIGHT_size, SSD2533_MIN_WEIGHT, int2byte(16)},
  /* Define Max. Finger Area (0-255) = 10 */
  {SSD2533_MAX_AREA_size, SSD2533_MAX_AREA, int2byte(10)},
			
  /* Set slicing depth in image segmentation process  = 41% */
 // {SSD2533_SEG_DEPTH_size, SSD2533_SEG_DEPTH, SSD2533_SEG_DEPTH_0_82},	//{ 1, 0x37, 0x00, 0x00},			
  /* Select CG calculation metod */
  {SSD2533_CG_METOD_size, SSD2533_CG_METOD, SSD2533_CG_METOD_Hybrid},
	
  /* Max finger */
  { 1, 0x8A, FINGERNO, 0x00}, //
  /* 1.5x mode */
  //{ 1, 0x8B, 0x01, 0x00},
  /* Edge compensation */
  { 1, 0x8C, 0xB0, 0x00},
  
  //{ 1, 0x3D, 0x02, 0x00},	
  /* XY Mapping  = Normal */
  {SSD2533_ORIENTATION_size, SSD2533_ORIENTATION, SSD2533_ORIENTATION_Normal},	//{ 1, 0x65, 0x01, 0x00},
  /* X Scaling */
  {SSD2533_X_SCALING_size, SSD2533_X_SCALING, 0x5E,0xD0},
  /* Y Scaling */
  { SSD2533_Y_SCALING_size,SSD2533_Y_SCALING,0x60, 0x00},

  /* Event mask */
 // {SSD2533_EVENT_MSK, SSD2533_EVENT_MSK_size, 0xff, 0xff},
  {SSD2533_EVENT_MSK_size, SSD2533_EVENT_MSK, 0xFF, 0xC7},	//{ 2, 0x7A, 0xFF, 0xFF},
  /* IRQ mask */
//{ 2, 0x7B, 0x00, 0x00},
//  {SSD2533_IRQ_MSK, SSD2533_IRQ_MSK_size, 0xff,0xef},
  {SSD2533_IRQ_MSK_size, SSD2533_IRQ_MSK, 0xff,0xf0},
  /* Set Operating Mode (Frame scan period in millisecond 0-255)  = 10 ms */
//  {SSD2533_WOP_MODE, SSD2533_WOP_MODE_size, 20}
};











struct ssd2533_ts_platform_data {
	unsigned int x_size;	/* X axis resolution */
	unsigned int y_size;	/* Y axis resolution */
};

struct ssd2533_i2c_ts_data {
	struct i2c_client *client;
	struct input_dev *input;
	const struct ssd2533_ts_platform_data *pdata;
	struct work_struct ssl_work;
	int irq;
	int gpio;
	
	int FingerNo;
	int FingerX[FINGERNO_MAX];
	int FingerY[FINGERNO_MAX];
	int FingerP[FINGERNO_MAX];
	int Resolution;
	int EventStatus;
	int FingerDetect;
	int sFingerX[FINGERNO_MAX];
	int sFingerY[FINGERNO_MAX];
	int pFingerX[FINGERNO_MAX];
	int pFingerY[FINGERNO_MAX];
};
static struct workqueue_struct *ssd253x_wq;

struct ChipSetting Reset[]={
{ 1, 0x04, 0x00, 0x00},	// SSD2533
};

int ssd253x_ReadRegister(struct i2c_client *client, char reg, unsigned char *buf, int size)
{
        struct i2c_msg msg[2];
        int ret;

        msg[0].addr = client->addr;
        msg[0].flags = 0;
        msg[0].len = 1;
        msg[0].buf = &reg;

        msg[1].addr = client->addr;
        msg[1].flags = I2C_M_RD;
        msg[1].len = size;
        msg[1].buf = buf;

        ret = i2c_transfer(client->adapter, msg, 2);

        //if(ret<0)       printk("                ssd253x_ReadRegister: i2c_transfer Error !\n");
        //else            printk("                ssd253x_ReadRegister: i2c_transfer OK !\n");

        return 0;
}

int ReadRegister(struct i2c_client *client,uint8_t reg,int ByteNo)
{
        unsigned char buf[4];
        struct i2c_msg msg[2];
        int ret;

        memset(buf, 0xFF, sizeof(buf));
        msg[0].addr = client->addr;
        msg[0].flags = 0;
        msg[0].len = 1;
        msg[0].buf = &reg;

        msg[1].addr = client->addr;
        msg[1].flags = I2C_M_RD;
        msg[1].len = ByteNo;
        msg[1].buf = buf;

        ret = i2c_transfer(client->adapter, msg, 2);

        //if(ret<0)       printk("                ReadRegister: i2c_transfer Error !\n");
        //else            printk("                ReadRegister: i2c_transfer OK !\n");

        if(ByteNo==1) return (int)((unsigned int)buf[0]<<0);
        if(ByteNo==2) return (int)((unsigned int)buf[1]<<0)|((unsigned int)buf[0]<<8);
        if(ByteNo==3) return (int)((unsigned int)buf[2]<<0)|((unsigned int)buf[1]<<8)|((unsigned int)buf[0]<<16);
        if(ByteNo==4) return (int)((unsigned int)buf[3]<<0)|((unsigned int)buf[2]<<8)|((unsigned int)buf[1]<<16)|(buf[0]<<24);
        return 0;
}

void WriteRegister(struct i2c_client *client,uint8_t Reg,unsigned char Data1,unsigned char Data2,int ByteNo)
{	
	struct i2c_msg msg;
	unsigned char buf[4];
	int ret;

	buf[0]=Reg;
	buf[1]=Data1;
	buf[2]=Data2;
	buf[3]=0;

	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = ByteNo+1;
	msg.buf = (char *)buf;

	ret = i2c_transfer(client->adapter, &msg, 1);

	//if(ret<0)	printk("		WriteRegister: i2c_master_send Error !\n");
	//else		printk("		WriteRegister: i2c_master_send OK !\n");
}

static void ssd253x_ts_work(struct work_struct *work)
{
	int i;
	unsigned short xpos=0, ypos=0, width=0;
	int FingerInfo;
	int EventStatus;
	int FingerX[FINGERNO];
	int FingerY[FINGERNO];
	int FingerP[FINGERNO];
	int clrFlag=0;
	
	//printk("ssd2533: in ssd253x_ts_work\n");
	
	tsdata_glob->FingerDetect=0;
	for(i=0; i<tsdata_glob->FingerNo; i++)
	{
		//printk("ssd2533: in ssd253x_ts_work (EventStatus>>i)&0x1 = true\n");
			
		FingerInfo=ReadRegister(tsdata_glob->client,FINGER01_REG+i,4);
		xpos = ((FingerInfo>>4)&0xF00)|((FingerInfo>>24)&0xFF);
		ypos = ((FingerInfo>>0)&0xF00)|((FingerInfo>>16)&0xFF);
		
		//width= ((FingerInfo>>4)&0x00F);	
		//printk("ssd2533: in ssd253x_ts_work FingerInfo = %d\n", FingerInfo);

		if(xpos!=0xFFF)
		{
			tsdata_glob->FingerDetect++;
		}
		else 
		{
			// This part is to avoid asyn problem when the finger leaves
			//printk("		ssd253x_ts_work: Correct %x\n",EventStatus);
			EventStatus=EventStatus&~(1<<i);
			clrFlag=1;
		}
			
		FingerX[i]=xpos;
		FingerY[i]=ypos;
		//FingerP[i]=width;
		
		//printk("ssd2533: xpos = %d\n", xpos);
		//printk("ssd2533: ypos = %d\n", ypos);
	}

	if(clrFlag) WriteRegister(tsdata_glob->client,EVENT_FIFO_SCLR,0x01,0x00,1);

	for(i=0; i<tsdata_glob->FingerNo; i++)
	{
		xpos=FingerX[i];
		ypos=FingerY[i];
		width=FingerP[i];

		if(xpos!=0xFFF)
		{
			if (i==0)
			{
				input_report_key(tsdata_glob->input, BTN_TOUCH,  1);
				input_report_abs(tsdata_glob->input, ABS_X, xpos);
				input_report_abs(tsdata_glob->input, ABS_Y, ypos);
			}

			input_report_abs(tsdata_glob->input, ABS_MT_POSITION_X, xpos);
			input_report_abs(tsdata_glob->input, ABS_MT_POSITION_Y, ypos);
			input_mt_sync(tsdata_glob->input);


			/*if(i==0) printk("		ssd253x_ts_work: N = %d, X = %d, Y = %d, W = %d\n",
					i,xpos,ypos,width);*/
		}
		else if(tsdata_glob->FingerX[i]!=0xFFF)
		{
			if (i==0)
			{
				input_report_key(tsdata_glob->input, BTN_TOUCH,  0);
				input_report_abs(tsdata_glob->input, ABS_X, tsdata_glob->FingerX[i]);
				input_report_abs(tsdata_glob->input, ABS_Y, tsdata_glob->FingerY[i]);
			}

			input_report_abs(tsdata_glob->input, ABS_MT_POSITION_X, xpos);
			input_report_abs(tsdata_glob->input, ABS_MT_POSITION_Y, ypos);
			input_mt_sync(tsdata_glob->input);

			/*if(i==0) printk("	release	ssd253x_ts_work: N = %d, X = %d, Y = %d, W = %d\n",
					i,tsdata_glob->FingerX[i],tsdata_glob->FingerY[i],width);*/
		}
		tsdata_glob->FingerX[i]=FingerX[i];
		tsdata_glob->FingerY[i]=FingerY[i];
		tsdata_glob->FingerP[i]=width;
		
		/*printk("ssd2533: tsdata_glob->FingerX[i] = %d\n", tsdata_glob->FingerX[i]);
		printk("ssd2533: tsdata_glob->FingerY[i] = %d\n", tsdata_glob->FingerY[i]);*/
	}		
	tsdata_glob->EventStatus=EventStatus;	
	input_sync(tsdata_glob->input);
}

static irqreturn_t ssd2533_ts_isr(int irq, void *dev_id)
{
	struct ssd2533_i2c_ts_data *tsdata = dev_id;
	disable_irq_nosync(tsdata->irq);
	
	queue_work(ssd253x_wq, &tsdata->ssl_work);
	enable_irq(tsdata->irq);
	
	return IRQ_HANDLED;
}





static ssize_t SSD253x_RAW_show(struct device *dev, struct device_attribute *attr, char *buf);

static DEVICE_ATTR(RAWdata,   S_IRUGO, SSD253x_RAW_show, NULL);
static DEVICE_ATTR(DELTAdata, S_IRUGO, SSD253x_RAW_show, NULL);

static struct attribute *SSD253x_attributes[] = {
	&dev_attr_RAWdata.attr,
	&dev_attr_DELTAdata.attr,
	NULL,
};

static struct attribute_group SSD253x_attr_group = {
        .attrs = SSD253x_attributes,
};

static ssize_t SSD253x_RAW_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	char *type_data_txt;
	char type_data_cmd = 0;
	if(attr == &dev_attr_RAWdata)
	{
	    type_data_txt = "RAW";	
	    type_data_cmd = 0x01;	
	}
	else if(attr == &dev_attr_DELTAdata)
	{
	    type_data_txt = "DELTA";	
	    type_data_cmd = 0x05;	
	}
	else return -EINVAL;
	
	// Enable Manu-Mode
	WriteRegister(tsdata_glob->client, 0x8D, 0x01, 0x00, 1);
	// Run frame scan once
	WriteRegister(tsdata_glob->client, 0x93, 0x00, type_data_cmd, 2);
	mdelay(100);
	// Select RAM bank 0
	WriteRegister(tsdata_glob->client, 0x8E, 0x00, 0x00,  1);
	// Set ROW position to 0
	WriteRegister(tsdata_glob->client, 0x8F, 0x00, 0x00,  1);
	// Set COL position to 0
	WriteRegister(tsdata_glob->client, 0x90, 0x00, 0x00,  1);

	char* outpos = buf + sprintf(buf, "SSD2533 %s Data %ux%u (DRVxSEN):\n",type_data_txt, DRIVENO, SENSENO);	
	int row,col;	
	for(row = 0; row<DRIVENO; row++)
	{
	    outpos+= sprintf(outpos,  "      ");	    
	    for(col = 0; col<SENSENO; col++)
	    {
		outpos+= sprintf(outpos,  "%04X ", ReadRegister(tsdata_glob->client, 0x92, 2));
	    }
	    outpos+= sprintf(outpos,  "\n");	    
	}
	return strlen(buf);
}





void deviceReset(struct i2c_client *client)
{	
	int i;
	for(i=0;i<sizeof(Reset)/sizeof(Reset[0]);i++)
	{
		WriteRegister(	client,Reset[i].Reg,
				Reset[i].Data1,Reset[i].Data2,
				Reset[i].No);
	}
	mdelay(300);
}

void SSD253xdeviceInit(struct i2c_client *client)
{	
	int i;
	for(i=0;i<sizeof(ssd253xcfgTable)/sizeof(ssd253xcfgTable[0]);i++)
	{
		WriteRegister(	client,ssd253xcfgTable[i].Reg,
				ssd253xcfgTable[i].Data1,ssd253xcfgTable[i].Data2,
				ssd253xcfgTable[i].No);
	}
	mdelay(500);
	WriteRegister(client,SSD2533_WOP_MODE, 10,0,SSD2533_WOP_MODE_size );
}

#if defined(CONFIG_OF)
static const struct of_device_id ssd2533_of_match[];

static struct ssd2533_ts_platform_data *ssd2533_parse_dt(struct device *dev)
{
	struct ssd2533_ts_platform_data *pdata;
	struct device_node *np = dev->of_node;
	const struct of_device_id *match;

	match = of_match_device(of_match_ptr(ssd2533_of_match), dev);
	if (!match)
		return ERR_PTR(-EINVAL);

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	if (of_property_read_u32(np, "x-size", &pdata->x_size)) {
		dev_err(dev, "Failed to get x-size property\n");
		return ERR_PTR(-EINVAL);
	}

	if (of_property_read_u32(np, "y-size", &pdata->y_size)) {
		dev_err(dev, "Failed to get y-size property\n");
		return ERR_PTR(-EINVAL);
	}

	dev_dbg(dev, "%s: x %d, y %d\n", __func__,
				pdata->x_size, pdata->y_size);

	return pdata;
}
#else
static struct ssd2533_ts_platform_data *ssd2533_parse_dt(struct device *dev)
{
	return NULL;
}
#endif

static int ssd2533_i2c_ts_probe(struct i2c_client *client,
					 const struct i2c_device_id *id)
{
	const struct ssd2533_ts_platform_data *pdata = client->dev.platform_data;
	struct device *dev = &client->dev;
	struct device_node *np = dev->of_node;
	struct ssd2533_i2c_ts_data *tsdata;
	struct input_dev *input;
	int error;
	unsigned char revid;

	if (np) {
		pdata = ssd2533_parse_dt(dev);
		if (IS_ERR(pdata))
			return PTR_ERR(pdata);
	} else if (!pdata) {
		dev_err(&client->dev, "platform data not defined\n");
		printk("ssd2533: platform data not defined\n");
		return -EINVAL;
	}

	tsdata = devm_kzalloc(dev, sizeof(*tsdata), GFP_KERNEL);
	if (!tsdata)
	{
		printk("ssd2533: error devm_kzalloc\n");
		return -ENOMEM;
	}
	
	dev_set_drvdata(&client->dev, tsdata);
	dev_info(&client->dev, "ssd2533: revid %x touchscreen, irq %d\n", revid, client->irq);

	input = devm_input_allocate_device(dev);
	if (!input) {
		printk("ssd2533: Failed to allocate input device\n");
		dev_err(&client->dev, "Failed to allocate input device\n");
		return -ENOMEM;
	}
	
	client_glob = client;

	tsdata->client = client;
	tsdata->input = input;
	tsdata->pdata = pdata;
	tsdata->irq = client->irq;
	tsdata->gpio = client->irq - IRQ_TO_GPIO_OFFSET;
	tsdata->FingerNo = FINGERNO;
	
	tsdata_glob = tsdata;

	input->name = client->name;
	input->id.bustype = BUS_I2C;
	input->dev.parent = &client->dev;
	input->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS) | BIT_MASK(EV_SYN) ;
	input->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH) | BIT_MASK(BTN_2);
	input->id.vendor  = 0xABCD;//0x2878; // Modify for Vendor ID

	input_set_drvdata(input, tsdata);

	deviceReset(client);
	SSD253xdeviceInit(client);
	
    WriteRegister(client,SSD2533_EVENT_FIFO_SCLR,0x01,0x00,SSD2533_EVENT_FIFO_SCLR_size); // clear Event FiFo

	printk("ssd2533 Device ID  : 0x%04X\n", ReadRegister(client, SSD2533_DEVICE_ID,SSD2533_DEVICE_ID_size));
	printk("ssd2533 Version ID  : 0x%04X\n", ReadRegister(client, SSD2533_VERSION_ID,SSD2533_VERSION_ID_size));

	/* set range of the parameters */
	input_set_abs_params(input, ABS_X, 0, 639, 0, 0);
	input_set_abs_params(input, ABS_Y, 0, 479, 0, 0);
	input_set_abs_params(input, ABS_PRESSURE, 0, 0x0f, 0, 0);
	
	error = sysfs_create_group(&client->dev.kobj, &SSD253x_attr_group);
	if (error)
		printk(KERN_ERR "ssd253x_ts: can't create fs group");
	
	INIT_WORK(&tsdata->ssl_work, ssd253x_ts_work);

	error = request_irq(client->irq, ssd2533_ts_isr, 
		IRQF_TRIGGER_FALLING , client->name, tsdata);
		
	printk("ssd2533: tsdata->irq = %d\n", tsdata->irq);
	printk("ssd2533: tsdata->gpio = %d\n", tsdata->gpio);
	printk("ssd2533: probe gpio_get_value = %d\n", gpio_get_value(tsdata->gpio));
				     
	if (error) {
		dev_err(dev, "failed to request irq %d\n", client->irq);
		printk("ssd2533: failed to request irq\n");
		return error;
	}

	error = input_register_device(input);
	if (error)
	{
		printk("ssd2533: failed input_register_device\n");
		return error;
	}

	i2c_set_clientdata(client, tsdata);

	device_init_wakeup(&client->dev, 1);

	return 0;
}

static const struct i2c_device_id ssd2533_i2c_ts_id[] = {
	{ "ssd2533_tsc", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ssd2533_i2c_ts_id);

#if defined(CONFIG_OF)
static const struct of_device_id ssd2533_of_match[] = {
	{ .compatible = "ssl,ssd2533-tsc", },
	{ }
};
MODULE_DEVICE_TABLE(of, ssd2533_of_match);
#endif

static struct i2c_driver ssd2533_i2c_ts_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "ssd2533_tsc",
		.of_match_table = of_match_ptr(ssd2533_of_match),
	},
	.probe		= ssd2533_i2c_ts_probe,
	.id_table	= ssd2533_i2c_ts_id,
};

static int __init ssd253x_ts_init(void)
{
        printk("+-----------------------------------------+\n");
        printk("|       ssd253x_ts_init                   |\n");
        printk("+-----------------------------------------+\n");

        ssd253x_wq = create_singlethread_workqueue("ssd253x_wq");
        
        if (!ssd253x_wq)
        {
                printk("                ssd253x_ts_init: create_singlethread_workqueue Error!\n");
                return -ENOMEM;
        }
        else
        {
                printk("                ssd253x_ts_init: create_singlethread_workqueue OK!\n");
        }

        return 0;
}

module_init(ssd253x_ts_init);
module_i2c_driver(ssd2533_i2c_ts_driver);

MODULE_AUTHOR("Anton Tishenko NPF ATI");
MODULE_DESCRIPTION("SSD2533 I2C Touchscreen Driver");
MODULE_LICENSE("GPL");
