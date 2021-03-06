/*****************************************************************************
 *
 * Filename:
 * ---------
 *     AR1335qtechmipi_Sensor.c
 *
 * Project:
 * --------
 *     ALPS
 *
 * Description:
 * ------------
 *     Source code of Sensor driver
 *
 *
 *------------------------------------------------------------------------------
 * Upper this line, this part is controlled by CC/CQ. DO NOT MODIFY!!
 *============================================================================
 ****************************************************************************/

#include <linux/videodev2.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <asm/atomic.h>
#include <linux/xlog.h>

#include "kd_camera_hw.h"
#include "kd_imgsensor.h"
#include "kd_imgsensor_define.h"
#include "kd_imgsensor_errcode.h"

#include "ar1335qtechmipi_Sensor.h"

/****************************Modify Following Strings for Debug****************************/
#define PFX "ar1335_qtech_camera_sensor"
#define LOG_1 LOG_INF("AR1335_QTECH,MIPI 4LANE\n")
#define LOG_2 LOG_INF("preview 2456*1842@30fps,533.33Mbps/lane; video 3840*2160@30fps,533.33Mbps/lane; capture 18M@15fps,533.33Mbps/lane\n")
/****************************   Modify end    *******************************************/
#define AR1335_DEBUG

#ifdef AR1335_DEBUG
	#define LOG_INF(format, args...)    xlog_printk(ANDROID_LOG_INFO   , PFX, "[%s] " format, __FUNCTION__, ##args)
	//#define LOG_INF(format, args...)    printk(ANDROID_LOG_INFO   , PFX, "[%s] " format, __FUNCTION__, ##args)
#else
	#define LOG_INF(fmt, arg...)
#endif

#define AR1335_QTECH_MODULE_ID 0x06

//#define LOG_INF(format, args...)    xlog_printk(ANDROID_LOG_INFO   , PFX, "[%s] " format, __FUNCTION__, ##args)

static DEFINE_SPINLOCK(imgsensor_drv_lock);

#define AR1335_OTP
#define i2c_speed 200
extern u16 AROTPData[];

extern int get_device_info(char* buf);//add for factory by wangyi
static int info_limit = 0;
static char factory_module_id[50] = {0};//modify for factory mode cheak id by miaolei@yulong.com 2015.05.26
static  kal_uint16 g_otp_id = 0;

static imgsensor_info_struct imgsensor_info = {
    .sensor_id = AR1335_SENSOR_ID,        //record sensor id defined in Kd_imgsensor.h

    .checksum_value = 0xa7f3e34,//0x722b3840,        //checksum value for Camera Auto Test

    .pre = {
        .pclk = 440000000,                //record different mode's pclk
        .linelength = 4608,                //record different mode's linelength
        .framelength = 3182,            //record different mode's framelength
        .startx = 0,                    //record different mode's startx of grabwindow
        .starty = 0,                    //record different mode's starty of grabwindow
        .grabwindow_width = 2104,        //record different mode's width of grabwindow
        .grabwindow_height = 1560,        //record different mode's height of grabwindow
        /*     following for MIPIDataLowPwr2HighSpeedSettleDelayCount by different scenario    */
        .mipi_data_lp2hs_settle_dc = 85,//unit , ns
        /*     following for GetDefaultFramerateByScenario()    */
        .max_framerate = 300,
    },
    .cap = {
        .pclk = 440000000,
        .linelength = 5726,
        .framelength = 3200,
        .startx = 0,
        .starty = 0,
        .grabwindow_width = 4208,
        .grabwindow_height = 3120,
        .mipi_data_lp2hs_settle_dc = 85,//unit , ns
        .max_framerate = 240,
    },
    .cap1 = {                            //capture for PIP 24fps relative information, capture1 mode must use same framelength, linelength with Capture mode for shutter calculate
        .pclk = 440000000,
        .linelength = 5726,
        .framelength = 3200,
        .startx = 0,
        .starty = 0,
        .grabwindow_width = 4208,
        .grabwindow_height = 3120,
        .mipi_data_lp2hs_settle_dc = 85,//unit , ns
        .max_framerate = 240,    //less than 24 fps13M(include 13M),cap1 max framerate is 24fps,16M max framerate is 20fps, 20M max framerate is 15fps
    },
    .normal_video = {
        .pclk = 440000000,
        .linelength = 4608,
        .framelength = 3182,
        .startx = 0,
        .starty = 0,
        .grabwindow_width = 1920,
        .grabwindow_height = 1080,
        .mipi_data_lp2hs_settle_dc = 85,//unit , ns
        .max_framerate = 300,
    },
	.hs_video = {
        .pclk = 440000000,
        .linelength = 4608,
        .framelength = 794,
        .startx = 0,
        .starty = 0,
        .grabwindow_width = 1280,
        .grabwindow_height = 720,
        .mipi_data_lp2hs_settle_dc = 85,//unit , ns
        .max_framerate = 1200,
    },
    .slim_video = {
		.pclk = 440000000,
        .linelength = 4608,
        .framelength = 3182,
        .startx = 0,
        .starty = 0,
        .grabwindow_width = 2104,
        .grabwindow_height = 1560,
        .mipi_data_lp2hs_settle_dc = 85,//unit , ns
        .max_framerate = 300,
    },
    .margin = 1,            //sensor framelength & shutter margin
    .min_shutter = 1,        //min shutter
    .max_frame_length = 0x7fff,//max framelength by sensor register's limitation
    .ae_shut_delay_frame = 0,    //shutter delay frame for AE cycle, 2 frame with ispGain_delay-shut_delay=2-0=2
    .ae_sensor_gain_delay_frame = 0,//sensor gain delay frame for AE cycle,2 frame with ispGain_delay-sensor_gain_delay=2-0=2
    .ae_ispGain_delay_frame = 2,//isp gain delay frame for AE cycle
    .ihdr_support = 0,      //1, support; 0,not support
    .ihdr_le_firstline = 0,  //1,le first ; 0, se first
    .sensor_mode_num = 5,      //support sensor mode num

    .cap_delay_frame = 2,        //enter capture delay frame num
    .pre_delay_frame = 2,         //enter preview delay frame num
    .video_delay_frame = 2,        //enter video delay frame num
    .hs_video_delay_frame = 2,    //enter high speed video  delay frame num
    .slim_video_delay_frame = 2,//enter slim video delay frame num

    .isp_driving_current = ISP_DRIVING_6MA, //mclk driving current
    .sensor_interface_type = SENSOR_INTERFACE_TYPE_MIPI,//sensor_interface_type
    .mipi_sensor_type = MIPI_OPHY_NCSI2, //0,MIPI_OPHY_NCSI2;  1,MIPI_OPHY_CSI2
    .mipi_settle_delay_mode = MIPI_SETTLEDELAY_AUTO,//0,MIPI_SETTLEDELAY_AUTO; 1,MIPI_SETTLEDELAY_MANNUAL
    .sensor_output_dataformat = SENSOR_OUTPUT_FORMAT_RAW_Gb,//sensor output first pixel color SENSOR_OUTPUT_FORMAT_RAW_Gr
    .mclk = 24,//mclk value, suggest 24 or 26 for 24Mhz or 26Mhz
    .mipi_lane_num = SENSOR_MIPI_4_LANE,//mipi lane num
    .i2c_addr_table = {0x6c,0xff},//record sensor support all write id addr, only supprt 4must end with 0xff
};


static imgsensor_struct imgsensor = {
    .mirror = IMAGE_HV_MIRROR,                //mirrorflip information IMAGE_HV_MIRROR  IMAGE_NORMAL
    .sensor_mode = IMGSENSOR_MODE_INIT, //IMGSENSOR_MODE enum value,record current sensor mode,such as: INIT, Preview, Capture, Video,High Speed Video, Slim Video
    .shutter = 0x0C4F,                    //current shutter
    .gain = 0x100,                        //current gain
    .dummy_pixel = 0,                    //current dummypixel
    .dummy_line = 0,                    //current dummyline
    .current_fps = 300,  //full size current fps : 24fps for PIP, 30fps for Normal or ZSD
    .autoflicker_en = KAL_FALSE,  //auto flicker enable: KAL_FALSE for disable auto flicker, KAL_TRUE for enable auto flicker
    .test_pattern = KAL_FALSE,        //test pattern mode or not. KAL_FALSE for in test pattern mode, KAL_TRUE for normal output
    .current_scenario_id = MSDK_SCENARIO_ID_CAMERA_PREVIEW,//current scenario id
    .ihdr_en = 0, //sensor need support LE, SE with HDR feature
    .i2c_write_id = 0x6c,//record current sensor's i2c write id
};


/* Sensor output window information */
static SENSOR_WINSIZE_INFO_STRUCT imgsensor_winsize_info[5] =
{{ 4224, 3136,	   16,	  16, 4208, 3120, 2104, 1560, 0000, 0000, 2104,  1560,		0,	  0, 2104, 1560}, // Preview
 { 4224, 3136,	   16,	  16, 4208, 3120, 4208, 3120, 0000, 0000, 4208,  3120,		0,	  0, 4208, 3120}, // capture
 { 4224, 3136,     16,   496, 4206, 2160, 1920, 1080, 0000, 0000, 1920,  1080,      0,    0, 1920, 1080}, // video
 { 4224, 3136,	  200,	 496, 3840, 2160, 1280,  720, 0000, 0000, 1280,   720,		0,	  0, 1280,	720},//hight speed video
 { 4224, 3136,	   16,	  16, 4208, 3120, 2104, 1560, 0000, 0000, 2104,  1560,		0,	  0, 2104, 1560}};// slim video


static kal_uint16 read_cmos_sensor(kal_uint32 addr)
{
    kal_uint16 get_byte=0;

    char pu_send_cmd[2] = {(char)(addr >> 8), (char)(addr & 0xFF) };
    kdSetI2CSpeed(i2c_speed); // Add this func to set i2c speed by each sensor
    iReadRegI2C(pu_send_cmd, 2, (u8*)&get_byte, 1, imgsensor.i2c_write_id);

    return get_byte;
}
static kal_uint16 read_cmos_sensor_2(kal_uint32 addr)
{
    kal_uint16 get_byte=0;
	kal_uint16 tmp = 0;
    kdSetI2CSpeed(i2c_speed); // Add this func to set i2c speed by each sensor
    char pu_send_cmd[2] = {(char)(addr >> 8), (char)(addr & 0xFF) };
    iReadRegI2C(pu_send_cmd, 2, (u8*)&get_byte, 2, imgsensor.i2c_write_id);

	tmp = get_byte >> 8;
	get_byte = ((get_byte & 0x00ff) << 8) | tmp;

    return get_byte;
}

static void write_cmos_sensor(kal_uint32 addr, kal_uint32 para)
{
    char pu_send_cmd[3] = {(char)(addr >> 8), (char)(addr & 0xFF), (char)(para & 0xFF)};
    kdSetI2CSpeed(i2c_speed); // Add this func to set i2c speed by each sensor
    iWriteRegI2C(pu_send_cmd, 3, imgsensor.i2c_write_id);
}

static void write_cmos_sensor_2byte(kal_uint32 addr, kal_uint32 para)
{
    char pu_send_cmd[4] = {(char)(addr >> 8), (char)(addr & 0xFF), (char)(para >> 8), (char)(para & 0xFF)};
    kdSetI2CSpeed(i2c_speed); // Add this func to set i2c speed by each sensor
    iWriteRegI2C(pu_send_cmd, 4, imgsensor.i2c_write_id);
}

static void set_dummy()
{
    LOG_INF("dummyline = %d, dummypixels = %d \n", imgsensor.dummy_line, imgsensor.dummy_pixel);
    /* you can set dummy by imgsensor.dummy_line and imgsensor.dummy_pixel, or you can set dummy by imgsensor.frame_length and imgsensor.line_length */
	write_cmos_sensor(0x0104, 0x01);
	write_cmos_sensor_2byte(0x0340, imgsensor.frame_length);
	write_cmos_sensor_2byte(0x0342, imgsensor.line_length);
	write_cmos_sensor(0x0104, 0x00);
}    /*    set_dummy  */

static kal_uint32 return_sensor_id()
{
    return ((read_cmos_sensor(0x0000) << 8) | read_cmos_sensor(0x0001));
}

#if defined(AR1335_OTP)
static kal_uint16 awb_R_Gr_ratio=1024;
static kal_uint16 awb_B_Gr_ratio= 1024;
static kal_uint16 awb_Gb_Gr_ratio= 1024;
/*
oly awb
static int RG_Ratio_Typical = 0x24f;
static int BG_Ratio_Typical = 0x290;
static int GG_Ratio_Typical = 0x400;
*/
// qtech awb
static int RG_Ratio_Typical = 0x229;
static int BG_Ratio_Typical = 0x241;
static int GG_Ratio_Typical = 0x400;


static void AR1335_otp_read(kal_uint16 otp_type){   // here type 1100 或者是其他type 3700
	kal_uint16 flag_end=0;
	int i=0;
	/// mclk=12 mhz
	write_cmos_sensor_2byte(0x301A,0x001D);  ///
	write_cmos_sensor_2byte(0x301A,0x0218);
	write_cmos_sensor_2byte(0x304C,(otp_type&0xff)<<8);  //
	write_cmos_sensor_2byte(0x3054,0x0400);
	write_cmos_sensor_2byte(0x304A,0x0210);

	do{
		flag_end = (read_cmos_sensor_2(0x304A));
		if((flag_end&0x0060) == 0x60){
			LOG_INF("AR1335 read otpm successful 0x304A = 0x%x  ,otp_type =0x%x \n",flag_end,otp_type);
			break;
		}
		mDELAY(10);
		i++;
		LOG_INF("AR1335 read otpm error 0x304A = 0x%x,i=%d  ,otp_type =0x%x \n",flag_end,i,otp_type);
	}while(i<3);
}

	// enable sc
static void AR1335_otp_read_awb()
{
	kal_uint16 temp,temp1;
		kal_uint16 type37_R3800;
		kal_uint16 type37_R3802;
		kal_uint16 type37_R3804;
		kal_uint16 type37_R3806;
		kal_uint16 Checksum1;
		kal_uint16 Checksum;
	AR1335_otp_read(0x30);
	temp = read_cmos_sensor_2(0x3800); /// check awb whether exist bit[3]AWB DATA EXIST bit: 0->no exit AWB data; 1->awb data exist
	LOG_INF("AR1335 read otpm AWB enter 30-3800 temp=0x %x !\n",temp);
	temp = temp&0x0008;
	LOG_INF("AR1335 read otpm AWB enter 1 30-3800 temp=0x %x !\n",temp);
	if(temp==0x0008)
		{
			LOG_INF("AR1335 read otpm AWB exist !!!!!\n");

				AR1335_otp_read(0x37); /// read AWB data;
				temp = read_cmos_sensor_2(0x3806);  // read the type37 flag

				LOG_INF("AR1335read otpm AWB enter 37- 3806 temp=0x %x !\n",temp);

				temp1 = read_cmos_sensor_2(0x304A);
				LOG_INF("AR1335 read otpm AWB enter 37- 304a temp1=0x %x !\n",temp1);
				temp1 = temp1&0x0040;
				LOG_INF("AR1335 read otpm AWB enter 5 30-304a temp1=0x %x !\n",temp1);
				if((temp ==0xFFFF)&&(temp1==0x0040))  // if the flag == 0xffff&0x304A[6]==1 then read AWB
				{
						type37_R3800 = read_cmos_sensor_2(0x3800);
						type37_R3802 = read_cmos_sensor_2(0x3802);
						type37_R3804 = read_cmos_sensor_2(0x3804);
						type37_R3806 = read_cmos_sensor_2(0x3806);
						awb_R_Gr_ratio   = type37_R3800;
						awb_B_Gr_ratio   = type37_R3802;
						awb_Gb_Gr_ratio  = type37_R3804;
						AROTPData[0] = awb_R_Gr_ratio;//
						AROTPData[1] = awb_B_Gr_ratio; //
						AROTPData[2] = awb_Gb_Gr_ratio; //
						AROTPData[3] = RG_Ratio_Typical;
						AROTPData[4] = BG_Ratio_Typical;
						AROTPData[5] = GG_Ratio_Typical;
						LOG_INF("AR1335 type37_R3800   =0x%x, type37_R3802 =0x%x,type37_R3804 =0x%x,type37_R3806 =0x%x,\n", type37_R3800,type37_R3802,type37_R3804,type37_R3806);
						Checksum1 = (type37_R3800+type37_R3802+type37_R3804)%65536+1;
						LOG_INF("AR1335read otpm AWB enter Checksum1 =0x %x !\n",Checksum1);
						AR1335_otp_read(0x38); /// read awb checksum data;
						Checksum = read_cmos_sensor_2(0x3800);
						LOG_INF("AR1335read otpm AWB enter Checksum =0x %x !\n",Checksum);
						if(Checksum1==Checksum)
							{
								awb_R_Gr_ratio   = type37_R3800;
								awb_B_Gr_ratio   = type37_R3802;
								awb_Gb_Gr_ratio  = type37_R3804;
								AROTPData[0] = awb_R_Gr_ratio;//
								AROTPData[1] = awb_B_Gr_ratio; //
								AROTPData[2] = awb_Gb_Gr_ratio; //
								AROTPData[3] = RG_Ratio_Typical;
								AROTPData[4] = BG_Ratio_Typical;
								AROTPData[5] = GG_Ratio_Typical;
								LOG_INF("AR1335 awb_R_Gr_ratio=0x%x, awb_B_Gr_ratio=0x%x,awb_Gb_Gr_ratio =0x%x,\n", awb_R_Gr_ratio,awb_B_Gr_ratio,awb_Gb_Gr_ratio);
							}
					}

		} //
	LOG_INF("AR1335 read otpm AWB exit !\n");


}
#endif
static void set_max_framerate(UINT16 framerate,kal_bool min_framelength_en)
{
    kal_int16 dummy_line;
    kal_uint32 frame_length = imgsensor.frame_length;
    //unsigned long flags;

    LOG_INF("framerate = %d, min framelength should enable? \n", framerate,min_framelength_en);

    frame_length = imgsensor.pclk / framerate * 10 / imgsensor.line_length;
    spin_lock(&imgsensor_drv_lock);
    imgsensor.frame_length = (frame_length > imgsensor.min_frame_length) ? frame_length : imgsensor.min_frame_length;
    imgsensor.dummy_line = imgsensor.frame_length - imgsensor.min_frame_length;
    //dummy_line = frame_length - imgsensor.min_frame_length;
    //if (dummy_line < 0)
        //imgsensor.dummy_line = 0;
    //else
        //imgsensor.dummy_line = dummy_line;
    //imgsensor.frame_length = frame_length + imgsensor.dummy_line;
    if (imgsensor.frame_length > imgsensor_info.max_frame_length)
    {
        imgsensor.frame_length = imgsensor_info.max_frame_length;
        imgsensor.dummy_line = imgsensor.frame_length - imgsensor.min_frame_length;
    }
    if (min_framelength_en)
        imgsensor.min_frame_length = imgsensor.frame_length;
    spin_unlock(&imgsensor_drv_lock);
    set_dummy();
}    /*    set_max_framerate  */



/*************************************************************************
* FUNCTION
*    set_shutter
*
* DESCRIPTION
*    This function set e-shutter of sensor to change exposure time.
*
* PARAMETERS
*    iShutter : exposured lines
*
* RETURNS
*    None
*
* GLOBALS AFFECTED
*
*************************************************************************/
static void set_shutter(kal_uint16 shutter)
{
    unsigned long flags;
    kal_uint16 realtime_fps = 0;
    kal_uint32 frame_length = 0;
    spin_lock_irqsave(&imgsensor_drv_lock, flags);
    imgsensor.shutter = shutter;
    spin_unlock_irqrestore(&imgsensor_drv_lock, flags);

	//add by lichao

	spin_lock(&imgsensor_drv_lock);
	if (shutter > imgsensor.min_frame_length - imgsensor_info.margin)
		imgsensor.frame_length = shutter + imgsensor_info.margin;
	else
		imgsensor.frame_length = imgsensor.min_frame_length;
	if (imgsensor.frame_length > imgsensor_info.max_frame_length)
		imgsensor.frame_length = imgsensor_info.max_frame_length;
	spin_unlock(&imgsensor_drv_lock);
	shutter = (shutter < imgsensor_info.min_shutter) ? imgsensor_info.min_shutter : shutter;//get bigger one
	shutter = (shutter > (imgsensor_info.max_frame_length - imgsensor_info.margin)) ? (imgsensor_info.max_frame_length - imgsensor_info.margin) : shutter;	//get smaller one
#if 1
	if (imgsensor.autoflicker_en)
	{
		realtime_fps = imgsensor.pclk / imgsensor.line_length * 10 / imgsensor.frame_length;
		if(realtime_fps >= 297 && realtime_fps <= 305)
			set_max_framerate(296,0);
		else if(realtime_fps >= 147 && realtime_fps <= 150)
			set_max_framerate(146,0);
		else
		{
			// Extend frame length
			//write_cmos_sensor(0x0104, 0x01);
			write_cmos_sensor_2byte(0x0340, imgsensor.frame_length); 	// FRAME_LENGTH_LINES
			//write_cmos_sensor(0x0104, 0x00);
		}
	}
	else
	{
		// Extend frame length
		//write_cmos_sensor(0x0104, 0x01);
		write_cmos_sensor_2byte(0x0340, imgsensor.frame_length); 	// FRAME_LENGTH_LINES
		//write_cmos_sensor(0x0104, 0x00);
	}
#endif

    // Update Shutter
    //write_cmos_sensor(0x0104, 0x01);
    write_cmos_sensor_2byte(0x0202, shutter);
	//write_cmos_sensor(0x0104, 0x00);
    LOG_INF("Exit! shutter =%d, framelength =%d\n", shutter,imgsensor.frame_length);

}    /*    set_shutter */


/*************************************************************************
* FUNCTION
*    set_gain
*
* DESCRIPTION
*    This function is to set global gain to sensor.
*
* PARAMETERS
*    iGain : sensor global gain(base: 0x40)
*
* RETURNS
*    the actually gain set to sensor.
*
* GLOBALS AFFECTED
*
*************************************************************************/
static kal_uint16 set_gain(kal_uint16 gain)
{

      kal_uint16 reg_gain;
	kal_uint16 gain_value02,Gain305E;
	kal_uint16 digital_gain = 64;
	kal_uint16 analog_coarse_gain =2;
	kal_uint16 analog_fine_gain = 0;

    //
    if (gain < BASEGAIN || gain > 32 * BASEGAIN) {
        LOG_INF("Error gain setting");

        if (gain < BASEGAIN)
            gain = BASEGAIN;
        else if (gain > 32 * BASEGAIN)
            gain = 32 * BASEGAIN;
    }
			/////not complete
    if(gain<2*BASEGAIN)
    	{
    		digital_gain = 64;
    		analog_coarse_gain = 1;
    		analog_fine_gain = (gain*16)/BASEGAIN -16;

    	}
    else if(gain<4*BASEGAIN)
  		{
  			digital_gain = 64;
    		analog_coarse_gain = 2;
    		analog_fine_gain = (gain*16)/(BASEGAIN*2) -16;
  		}
     else if(gain<(kal_uint16)(7.75*BASEGAIN))
  		{
			digital_gain = 64;
			analog_coarse_gain = 3;
			analog_fine_gain = (gain*16)/(BASEGAIN*4) -16;
  		}
  	else
  		{
                analog_coarse_gain = 3;
                analog_fine_gain = 0xf;
  		digital_gain =  (gain*16*64)/(BASEGAIN*4*(16+analog_fine_gain));
  		}
       reg_gain= ((digital_gain & 0x1ff) << 7 )|((analog_coarse_gain & 0x7) << 4) | (analog_fine_gain & 0xf);
	//write_cmos_sensor(0x0104, 0x01);
	write_cmos_sensor_2byte(0x305E, reg_gain);
	//write_cmos_sensor(0x0104, 0x00);
	spin_lock(&imgsensor_drv_lock);
	imgsensor.gain = reg_gain;
	spin_unlock(&imgsensor_drv_lock);
       LOG_INF("gain = %d , reg_gain = 0x%x\n ", gain,reg_gain);

    return gain;
}    /*    set_gain  */

static void ihdr_write_shutter_gain(kal_uint16 le, kal_uint16 se, kal_uint16 gain)
{
    LOG_INF("le:0x%x, se:0x%x, gain:0x%x\n",le,se,gain);
    if (imgsensor.ihdr_en) {

        spin_lock(&imgsensor_drv_lock);
        if (le > imgsensor.min_frame_length - imgsensor_info.margin)
            imgsensor.frame_length = le + imgsensor_info.margin;
        else
            imgsensor.frame_length = imgsensor.min_frame_length;
        if (imgsensor.frame_length > imgsensor_info.max_frame_length)
            imgsensor.frame_length = imgsensor_info.max_frame_length;
        spin_unlock(&imgsensor_drv_lock);
        if (le < imgsensor_info.min_shutter) le = imgsensor_info.min_shutter;
        if (se < imgsensor_info.min_shutter) se = imgsensor_info.min_shutter;


        // Extend frame length first
        //write_cmos_sensor(0x380e, imgsensor.frame_length >> 8);
        //write_cmos_sensor(0x380f, imgsensor.frame_length & 0xFF);

        //write_cmos_sensor(0x3502, (le << 4) & 0xFF);
        //write_cmos_sensor(0x3501, (le >> 4) & 0xFF);
        //write_cmos_sensor(0x3500, (le >> 12) & 0x0F);

        //write_cmos_sensor(0x3508, (se << 4) & 0xFF);
        //write_cmos_sensor(0x3507, (se >> 4) & 0xFF);
        //write_cmos_sensor(0x3506, (se >> 12) & 0x0F);

        //set_gain(gain);
    }

}



static void set_mirror_flip(kal_uint8 image_mirror)
{
    LOG_INF("image_mirror = %d\n", image_mirror);

    /********************************************************
       *
       *   0x3820[2] ISP Vertical flip
       *   0x3820[1] Sensor Vertical flip
       *
       *   0x3821[2] ISP Horizontal mirror
       *   0x3821[1] Sensor Horizontal mirror
       *
       *   ISP and Sensor flip or mirror register bit should be the same!!
       *
       ********************************************************/

    switch (image_mirror) {
        case IMAGE_NORMAL:
            write_cmos_sensor(0x0101,((read_cmos_sensor(0x0101) & 0xFC) | 0x00));
            break;
        case IMAGE_H_MIRROR:
            write_cmos_sensor(0x0101,((read_cmos_sensor(0x0101) & 0xFC) | 0x01));
            break;
        case IMAGE_V_MIRROR:
            write_cmos_sensor(0x0101,((read_cmos_sensor(0x0101) & 0xFC) | 0x02));
            break;
        case IMAGE_HV_MIRROR:
            write_cmos_sensor(0x0101,((read_cmos_sensor(0x0101) & 0xFC) | 0x03));
            break;
        default:
            LOG_INF("Error image_mirror setting\n");
    }

}

/*************************************************************************
* FUNCTION
*    night_mode
*
* DESCRIPTION
*    This function night mode of sensor.
*
* PARAMETERS
*    bEnable: KAL_TRUE -> enable night mode, otherwise, disable night mode
*
* RETURNS
*    None
*
* GLOBALS AFFECTED
*
*************************************************************************/
static void night_mode(kal_bool enable)
{
/*No Need to implement this function*/
}    /*    night_mode    */

static void sensor_init(void)
{
    LOG_INF("sensor_init Enter \n");

   /*****************************************************************************
    0x3098[0:1] pll3_prediv
    pll3_prediv_map[] = {2, 3, 4, 6}

    0x3099[0:4] pll3_multiplier
    pll3_multiplier

    0x309C[0] pll3_rdiv
    pll3_rdiv + 1

    0x309A[0:3] pll3_sys_div
    pll3_sys_div + 1

    0x309B[0:1] pll3_div
    pll3_div[] = {2, 2, 4, 5}

    VCO = XVCLK * 2 / pll3_prediv * pll3_multiplier * pll3_rdiv
    sysclk = VCO * 2 * 2 / pll3_sys_div / pll3_div

    XVCLK = 24 MHZ
    0x3098, 0x03
    0x3099, 0x1e
    0x309a, 0x02
    0x309b, 0x01
    0x309c, 0x00


    VCO = 24 * 2 / 6 * 31 * 1
    sysclk = VCO * 2  * 2 / 3 / 2
    sysclk = 160 MHZ
    */

   write_cmos_sensor(0x0103, 0x01); 	// SOFTWARE_RESET     8bit
	mDELAY(20);
////corrections_recommended in here
write_cmos_sensor_2byte(0x3042, 0x1004); 	// DARK_CONTROL2
write_cmos_sensor_2byte(0x30D2, 0x0120); 	// CRM_CONTROL
write_cmos_sensor_2byte(0x30D4, 0x0000); 	// COLUMN_CORRECTION
write_cmos_sensor_2byte(0x3090, 0x0000); 	// RNF_CONTROL
write_cmos_sensor_2byte(0x30FC, 0x0060); 	// MODULE_CLK_OFF
write_cmos_sensor_2byte(0x30FE, 0x0060); 	// CTREE_OFF
write_cmos_sensor_2byte(0x31E0, 0x0781); 	// PIX_DEF_ID
write_cmos_sensor_2byte(0x3180, 0x9434); 	// FINE_DIG_CORRECTION_CONTROL
write_cmos_sensor_2byte(0x317C, 0xEFF4); 	// ANALOG_CONTROL7
write_cmos_sensor_2byte(0x30EE, 0x613E); 	// DARK_CONTROL3  0x4140
write_cmos_sensor_2byte(0x3F2C, 0x4428); 	// GTH_THRES_RTN

///pixel_timing_recommended in here
write_cmos_sensor_2byte(0x3D00, 0x0446); 	// DYNAMIC_SEQRAM_00
write_cmos_sensor_2byte(0x3D02, 0x4C66); 	// DYNAMIC_SEQRAM_02
write_cmos_sensor_2byte(0x3D04, 0xFFFF); 	// DYNAMIC_SEQRAM_04
write_cmos_sensor_2byte(0x3D06, 0xFFFF); 	// DYNAMIC_SEQRAM_06
write_cmos_sensor_2byte(0x3D08, 0x5E40); 	// DYNAMIC_SEQRAM_08
write_cmos_sensor_2byte(0x3D0A, 0x1146); 	// DYNAMIC_SEQRAM_0A
write_cmos_sensor_2byte(0x3D0C, 0x5D41); 	// DYNAMIC_SEQRAM_0C
write_cmos_sensor_2byte(0x3D0E, 0x1088); 	// DYNAMIC_SEQRAM_0E
write_cmos_sensor_2byte(0x3D10, 0x8342); 	// DYNAMIC_SEQRAM_10
write_cmos_sensor_2byte(0x3D12, 0x00C0); 	// DYNAMIC_SEQRAM_12
write_cmos_sensor_2byte(0x3D14, 0x5580); 	// DYNAMIC_SEQRAM_14
write_cmos_sensor_2byte(0x3D16, 0x5B83); 	// DYNAMIC_SEQRAM_16
write_cmos_sensor_2byte(0x3D18, 0x6084); 	// DYNAMIC_SEQRAM_18
write_cmos_sensor_2byte(0x3D1A, 0x5A8D); 	// DYNAMIC_SEQRAM_1A
write_cmos_sensor_2byte(0x3D1C, 0x00C0); 	// DYNAMIC_SEQRAM_1C
write_cmos_sensor_2byte(0x3D1E, 0x8342); 	// DYNAMIC_SEQRAM_1E
write_cmos_sensor_2byte(0x3D20, 0x925A); 	// DYNAMIC_SEQRAM_20
write_cmos_sensor_2byte(0x3D22, 0x8664); 	// DYNAMIC_SEQRAM_22
write_cmos_sensor_2byte(0x3D24, 0x1030); 	// DYNAMIC_SEQRAM_24
write_cmos_sensor_2byte(0x3D26, 0x801C); 	// DYNAMIC_SEQRAM_26
write_cmos_sensor_2byte(0x3D28, 0x00A0); 	// DYNAMIC_SEQRAM_28
write_cmos_sensor_2byte(0x3D2A, 0x56B0); 	// DYNAMIC_SEQRAM_2A
write_cmos_sensor_2byte(0x3D2C, 0x5788); 	// DYNAMIC_SEQRAM_2C
write_cmos_sensor_2byte(0x3D2E, 0x5150); 	// DYNAMIC_SEQRAM_2E
write_cmos_sensor_2byte(0x3D30, 0x824D); 	// DYNAMIC_SEQRAM_30
write_cmos_sensor_2byte(0x3D32, 0x8D58); 	// DYNAMIC_SEQRAM_32
write_cmos_sensor_2byte(0x3D34, 0x58D2); 	// DYNAMIC_SEQRAM_34
write_cmos_sensor_2byte(0x3D36, 0x438A); 	// DYNAMIC_SEQRAM_36
write_cmos_sensor_2byte(0x3D38, 0x4592); 	// DYNAMIC_SEQRAM_38
write_cmos_sensor_2byte(0x3D3A, 0x458A); 	// DYNAMIC_SEQRAM_3A
write_cmos_sensor_2byte(0x3D3C, 0x439D); 	// DYNAMIC_SEQRAM_3C
write_cmos_sensor_2byte(0x3D3E, 0x51CA); 	// DYNAMIC_SEQRAM_3E
write_cmos_sensor_2byte(0x3D40, 0x5182); 	// DYNAMIC_SEQRAM_40
write_cmos_sensor_2byte(0x3D42, 0x100C); 	// DYNAMIC_SEQRAM_42
write_cmos_sensor_2byte(0x3D44, 0x9259); 	// DYNAMIC_SEQRAM_44
write_cmos_sensor_2byte(0x3D46, 0x5982); 	// DYNAMIC_SEQRAM_46
write_cmos_sensor_2byte(0x3D48, 0x5FF7); 	// DYNAMIC_SEQRAM_48
write_cmos_sensor_2byte(0x3D4A, 0x6182); 	// DYNAMIC_SEQRAM_4A
write_cmos_sensor_2byte(0x3D4C, 0x6283); 	// DYNAMIC_SEQRAM_4C
write_cmos_sensor_2byte(0x3D4E, 0x4281); 	// DYNAMIC_SEQRAM_4E
write_cmos_sensor_2byte(0x3D50, 0x10C0); 	// DYNAMIC_SEQRAM_50
write_cmos_sensor_2byte(0x3D52, 0x6498); 	// DYNAMIC_SEQRAM_52
write_cmos_sensor_2byte(0x3D54, 0x4281); 	// DYNAMIC_SEQRAM_54
write_cmos_sensor_2byte(0x3D56, 0x41FF); 	// DYNAMIC_SEQRAM_56
write_cmos_sensor_2byte(0x3D58, 0xFFB8); 	// DYNAMIC_SEQRAM_58
write_cmos_sensor_2byte(0x3D5A, 0x4081); 	// DYNAMIC_SEQRAM_5A
write_cmos_sensor_2byte(0x3D5C, 0x4080); 	// DYNAMIC_SEQRAM_5C
write_cmos_sensor_2byte(0x3D5E, 0x4180); 	// DYNAMIC_SEQRAM_5E
write_cmos_sensor_2byte(0x3D60, 0x4280); 	// DYNAMIC_SEQRAM_60
write_cmos_sensor_2byte(0x3D62, 0x438D); 	// DYNAMIC_SEQRAM_62
write_cmos_sensor_2byte(0x3D64, 0x44BA); 	// DYNAMIC_SEQRAM_64
write_cmos_sensor_2byte(0x3D66, 0x4488); 	// DYNAMIC_SEQRAM_66
write_cmos_sensor_2byte(0x3D68, 0x4380); 	// DYNAMIC_SEQRAM_68
write_cmos_sensor_2byte(0x3D6A, 0x4241); 	// DYNAMIC_SEQRAM_6A
write_cmos_sensor_2byte(0x3D6C, 0x8140); 	// DYNAMIC_SEQRAM_6C
write_cmos_sensor_2byte(0x3D6E, 0x8240); 	// DYNAMIC_SEQRAM_6E
write_cmos_sensor_2byte(0x3D70, 0x8041); 	// DYNAMIC_SEQRAM_70
write_cmos_sensor_2byte(0x3D72, 0x8042); 	// DYNAMIC_SEQRAM_72
write_cmos_sensor_2byte(0x3D74, 0x8043); 	// DYNAMIC_SEQRAM_74
write_cmos_sensor_2byte(0x3D76, 0x8D44); 	// DYNAMIC_SEQRAM_76
write_cmos_sensor_2byte(0x3D78, 0xBA44); 	// DYNAMIC_SEQRAM_78
write_cmos_sensor_2byte(0x3D7A, 0x875E); 	// DYNAMIC_SEQRAM_7A
write_cmos_sensor_2byte(0x3D7C, 0x4354); 	// DYNAMIC_SEQRAM_7C
write_cmos_sensor_2byte(0x3D7E, 0x4241); 	// DYNAMIC_SEQRAM_7E
write_cmos_sensor_2byte(0x3D80, 0x8140); 	// DYNAMIC_SEQRAM_80
write_cmos_sensor_2byte(0x3D82, 0x8120); 	// DYNAMIC_SEQRAM_82
write_cmos_sensor_2byte(0x3D84, 0x2881); 	// DYNAMIC_SEQRAM_84
write_cmos_sensor_2byte(0x3D86, 0x6026); 	// DYNAMIC_SEQRAM_86
write_cmos_sensor_2byte(0x3D88, 0x8055); 	// DYNAMIC_SEQRAM_88
write_cmos_sensor_2byte(0x3D8A, 0x8070); 	// DYNAMIC_SEQRAM_8A
write_cmos_sensor_2byte(0x3D8C, 0x8040); 	// DYNAMIC_SEQRAM_8C
write_cmos_sensor_2byte(0x3D8E, 0x4C81); 	// DYNAMIC_SEQRAM_8E
write_cmos_sensor_2byte(0x3D90, 0x45C3); 	// DYNAMIC_SEQRAM_90
write_cmos_sensor_2byte(0x3D92, 0x4581); 	// DYNAMIC_SEQRAM_92
write_cmos_sensor_2byte(0x3D94, 0x4C40); 	// DYNAMIC_SEQRAM_94
write_cmos_sensor_2byte(0x3D96, 0x8070); 	// DYNAMIC_SEQRAM_96
write_cmos_sensor_2byte(0x3D98, 0x8040); 	// DYNAMIC_SEQRAM_98
write_cmos_sensor_2byte(0x3D9A, 0x4C85); 	// DYNAMIC_SEQRAM_9A
write_cmos_sensor_2byte(0x3D9C, 0x6CA8); 	// DYNAMIC_SEQRAM_9C
write_cmos_sensor_2byte(0x3D9E, 0x6C8C); 	// DYNAMIC_SEQRAM_9E
write_cmos_sensor_2byte(0x3DA0, 0x000E); 	// DYNAMIC_SEQRAM_A0
write_cmos_sensor_2byte(0x3DA2, 0xBE44); 	// DYNAMIC_SEQRAM_A2
write_cmos_sensor_2byte(0x3DA4, 0x8844); 	// DYNAMIC_SEQRAM_A4
write_cmos_sensor_2byte(0x3DA6, 0xBC78); 	// DYNAMIC_SEQRAM_A6
write_cmos_sensor_2byte(0x3DA8, 0x0900); 	// DYNAMIC_SEQRAM_A8
write_cmos_sensor_2byte(0x3DAA, 0x8904); 	// DYNAMIC_SEQRAM_AA
write_cmos_sensor_2byte(0x3DAC, 0x8080); 	// DYNAMIC_SEQRAM_AC
write_cmos_sensor_2byte(0x3DAE, 0x0240); 	// DYNAMIC_SEQRAM_AE
write_cmos_sensor_2byte(0x3DB0, 0x8609); 	// DYNAMIC_SEQRAM_B0
write_cmos_sensor_2byte(0x3DB2, 0x008E); 	// DYNAMIC_SEQRAM_B2
write_cmos_sensor_2byte(0x3DB4, 0x0900); 	// DYNAMIC_SEQRAM_B4
write_cmos_sensor_2byte(0x3DB6, 0x8002); 	// DYNAMIC_SEQRAM_B6
write_cmos_sensor_2byte(0x3DB8, 0x4080); 	// DYNAMIC_SEQRAM_B8
write_cmos_sensor_2byte(0x3DBA, 0x0480); 	// DYNAMIC_SEQRAM_BA
write_cmos_sensor_2byte(0x3DBC, 0x887C); 	// DYNAMIC_SEQRAM_BC
write_cmos_sensor_2byte(0x3DBE, 0xAA86); 	// DYNAMIC_SEQRAM_BE
write_cmos_sensor_2byte(0x3DC0, 0x0900); 	// DYNAMIC_SEQRAM_C0
write_cmos_sensor_2byte(0x3DC2, 0x877A); 	// DYNAMIC_SEQRAM_C2
write_cmos_sensor_2byte(0x3DC4, 0x000E); 	// DYNAMIC_SEQRAM_C4
write_cmos_sensor_2byte(0x3DC6, 0xC379); 	// DYNAMIC_SEQRAM_C6
write_cmos_sensor_2byte(0x3DC8, 0x4C40); 	// DYNAMIC_SEQRAM_C8
write_cmos_sensor_2byte(0x3DCA, 0xBF70); 	// DYNAMIC_SEQRAM_CA
write_cmos_sensor_2byte(0x3DCC, 0x5E40); 	// DYNAMIC_SEQRAM_CC
write_cmos_sensor_2byte(0x3DCE, 0x114E); 	// DYNAMIC_SEQRAM_CE
write_cmos_sensor_2byte(0x3DD0, 0x5D41); 	// DYNAMIC_SEQRAM_D0
write_cmos_sensor_2byte(0x3DD2, 0x5383); 	// DYNAMIC_SEQRAM_D2
write_cmos_sensor_2byte(0x3DD4, 0x4200); 	// DYNAMIC_SEQRAM_D4
write_cmos_sensor_2byte(0x3DD6, 0xC055); 	// DYNAMIC_SEQRAM_D6
write_cmos_sensor_2byte(0x3DD8, 0xA400); 	// DYNAMIC_SEQRAM_D8
write_cmos_sensor_2byte(0x3DDA, 0xC083); 	// DYNAMIC_SEQRAM_DA
write_cmos_sensor_2byte(0x3DDC, 0x4288); 	// DYNAMIC_SEQRAM_DC
write_cmos_sensor_2byte(0x3DDE, 0x6083); 	// DYNAMIC_SEQRAM_DE
write_cmos_sensor_2byte(0x3DE0, 0x5B80); 	// DYNAMIC_SEQRAM_E0
write_cmos_sensor_2byte(0x3DE2, 0x5A64); 	// DYNAMIC_SEQRAM_E2
write_cmos_sensor_2byte(0x3DE4, 0x1030); 	// DYNAMIC_SEQRAM_E4
write_cmos_sensor_2byte(0x3DE6, 0x801C); 	// DYNAMIC_SEQRAM_E6
write_cmos_sensor_2byte(0x3DE8, 0x00A5); 	// DYNAMIC_SEQRAM_E8
write_cmos_sensor_2byte(0x3DEA, 0x5697); 	// DYNAMIC_SEQRAM_EA
write_cmos_sensor_2byte(0x3DEC, 0x57A5); 	// DYNAMIC_SEQRAM_EC
write_cmos_sensor_2byte(0x3DEE, 0x5180); 	// DYNAMIC_SEQRAM_EE
write_cmos_sensor_2byte(0x3DF0, 0x505A); 	// DYNAMIC_SEQRAM_F0
write_cmos_sensor_2byte(0x3DF2, 0x814D); 	// DYNAMIC_SEQRAM_F2
write_cmos_sensor_2byte(0x3DF4, 0x8358); 	// DYNAMIC_SEQRAM_F4
write_cmos_sensor_2byte(0x3DF6, 0x8058); 	// DYNAMIC_SEQRAM_F6
write_cmos_sensor_2byte(0x3DF8, 0xA943); 	// DYNAMIC_SEQRAM_F8
write_cmos_sensor_2byte(0x3DFA, 0x8345); 	// DYNAMIC_SEQRAM_FA
write_cmos_sensor_2byte(0x3DFC, 0xB045); 	// DYNAMIC_SEQRAM_FC
write_cmos_sensor_2byte(0x3DFE, 0x8343); 	// DYNAMIC_SEQRAM_FE
write_cmos_sensor_2byte(0x3E00, 0xA351); 	// DYNAMIC_SEQRAM_100
write_cmos_sensor_2byte(0x3E02, 0xE251); 	// DYNAMIC_SEQRAM_102
write_cmos_sensor_2byte(0x3E04, 0x8C59); 	// DYNAMIC_SEQRAM_104
write_cmos_sensor_2byte(0x3E06, 0x8059); 	// DYNAMIC_SEQRAM_106
write_cmos_sensor_2byte(0x3E08, 0x8A5F); 	// DYNAMIC_SEQRAM_108
write_cmos_sensor_2byte(0x3E0A, 0xEC7C); 	// DYNAMIC_SEQRAM_10A
write_cmos_sensor_2byte(0x3E0C, 0xCC84); 	// DYNAMIC_SEQRAM_10C
write_cmos_sensor_2byte(0x3E0E, 0x6182); 	// DYNAMIC_SEQRAM_10E
write_cmos_sensor_2byte(0x3E10, 0x6283); 	// DYNAMIC_SEQRAM_110
write_cmos_sensor_2byte(0x3E12, 0x4283); 	// DYNAMIC_SEQRAM_112
write_cmos_sensor_2byte(0x3E14, 0x10CC); 	// DYNAMIC_SEQRAM_114
write_cmos_sensor_2byte(0x3E16, 0x6496); 	// DYNAMIC_SEQRAM_116
write_cmos_sensor_2byte(0x3E18, 0x4281); 	// DYNAMIC_SEQRAM_118
write_cmos_sensor_2byte(0x3E1A, 0x41BB); 	// DYNAMIC_SEQRAM_11A
write_cmos_sensor_2byte(0x3E1C, 0x4082); 	// DYNAMIC_SEQRAM_11C
write_cmos_sensor_2byte(0x3E1E, 0x407E); 	// DYNAMIC_SEQRAM_11E
write_cmos_sensor_2byte(0x3E20, 0xCC41); 	// DYNAMIC_SEQRAM_120
write_cmos_sensor_2byte(0x3E22, 0x8042); 	// DYNAMIC_SEQRAM_122
write_cmos_sensor_2byte(0x3E24, 0x8043); 	// DYNAMIC_SEQRAM_124
write_cmos_sensor_2byte(0x3E26, 0x8300); 	// DYNAMIC_SEQRAM_126
write_cmos_sensor_2byte(0x3E28, 0xC088); 	// DYNAMIC_SEQRAM_128
write_cmos_sensor_2byte(0x3E2A, 0x44BA); 	// DYNAMIC_SEQRAM_12A
write_cmos_sensor_2byte(0x3E2C, 0x4488); 	// DYNAMIC_SEQRAM_12C
write_cmos_sensor_2byte(0x3E2E, 0x00C8); 	// DYNAMIC_SEQRAM_12E
write_cmos_sensor_2byte(0x3E30, 0x8042); 	// DYNAMIC_SEQRAM_130
write_cmos_sensor_2byte(0x3E32, 0x4181); 	// DYNAMIC_SEQRAM_132
write_cmos_sensor_2byte(0x3E34, 0x4082); 	// DYNAMIC_SEQRAM_134
write_cmos_sensor_2byte(0x3E36, 0x4080); 	// DYNAMIC_SEQRAM_136
write_cmos_sensor_2byte(0x3E38, 0x4180); 	// DYNAMIC_SEQRAM_138
write_cmos_sensor_2byte(0x3E3A, 0x4280); 	// DYNAMIC_SEQRAM_13A
write_cmos_sensor_2byte(0x3E3C, 0x4383); 	// DYNAMIC_SEQRAM_13C
write_cmos_sensor_2byte(0x3E3E, 0x00C0); 	// DYNAMIC_SEQRAM_13E
write_cmos_sensor_2byte(0x3E40, 0x8844); 	// DYNAMIC_SEQRAM_140
write_cmos_sensor_2byte(0x3E42, 0xBA44); 	// DYNAMIC_SEQRAM_142
write_cmos_sensor_2byte(0x3E44, 0x8800); 	// DYNAMIC_SEQRAM_144
write_cmos_sensor_2byte(0x3E46, 0xC880); 	// DYNAMIC_SEQRAM_146
write_cmos_sensor_2byte(0x3E48, 0x4241); 	// DYNAMIC_SEQRAM_148
write_cmos_sensor_2byte(0x3E4A, 0x8240); 	// DYNAMIC_SEQRAM_14A
write_cmos_sensor_2byte(0x3E4C, 0x8140); 	// DYNAMIC_SEQRAM_14C
write_cmos_sensor_2byte(0x3E4E, 0x8041); 	// DYNAMIC_SEQRAM_14E
write_cmos_sensor_2byte(0x3E50, 0x8042); 	// DYNAMIC_SEQRAM_150
write_cmos_sensor_2byte(0x3E52, 0x8043); 	// DYNAMIC_SEQRAM_152
write_cmos_sensor_2byte(0x3E54, 0x8300); 	// DYNAMIC_SEQRAM_154
write_cmos_sensor_2byte(0x3E56, 0xC088); 	// DYNAMIC_SEQRAM_156
write_cmos_sensor_2byte(0x3E58, 0x44BA); 	// DYNAMIC_SEQRAM_158
write_cmos_sensor_2byte(0x3E5A, 0x4488); 	// DYNAMIC_SEQRAM_15A
write_cmos_sensor_2byte(0x3E5C, 0x00C8); 	// DYNAMIC_SEQRAM_15C
write_cmos_sensor_2byte(0x3E5E, 0x8042); 	// DYNAMIC_SEQRAM_15E
write_cmos_sensor_2byte(0x3E60, 0x4181); 	// DYNAMIC_SEQRAM_160
write_cmos_sensor_2byte(0x3E62, 0x4082); 	// DYNAMIC_SEQRAM_162
write_cmos_sensor_2byte(0x3E64, 0x4080); 	// DYNAMIC_SEQRAM_164
write_cmos_sensor_2byte(0x3E66, 0x4180); 	// DYNAMIC_SEQRAM_166
write_cmos_sensor_2byte(0x3E68, 0x4280); 	// DYNAMIC_SEQRAM_168
write_cmos_sensor_2byte(0x3E6A, 0x4383); 	// DYNAMIC_SEQRAM_16A
write_cmos_sensor_2byte(0x3E6C, 0x00C0); 	// DYNAMIC_SEQRAM_16C
write_cmos_sensor_2byte(0x3E6E, 0x8844); 	// DYNAMIC_SEQRAM_16E
write_cmos_sensor_2byte(0x3E70, 0xBA44); 	// DYNAMIC_SEQRAM_170
write_cmos_sensor_2byte(0x3E72, 0x8800); 	// DYNAMIC_SEQRAM_172
write_cmos_sensor_2byte(0x3E74, 0xC880); 	// DYNAMIC_SEQRAM_174
write_cmos_sensor_2byte(0x3E76, 0x4241); 	// DYNAMIC_SEQRAM_176
write_cmos_sensor_2byte(0x3E78, 0x8140); 	// DYNAMIC_SEQRAM_178
write_cmos_sensor_2byte(0x3E7A, 0x9F5E); 	// DYNAMIC_SEQRAM_17A
write_cmos_sensor_2byte(0x3E7C, 0x8A54); 	// DYNAMIC_SEQRAM_17C
write_cmos_sensor_2byte(0x3E7E, 0x8620); 	// DYNAMIC_SEQRAM_17E
write_cmos_sensor_2byte(0x3E80, 0x2881); 	// DYNAMIC_SEQRAM_180
write_cmos_sensor_2byte(0x3E82, 0x6026); 	// DYNAMIC_SEQRAM_182
write_cmos_sensor_2byte(0x3E84, 0x8055); 	// DYNAMIC_SEQRAM_184
write_cmos_sensor_2byte(0x3E86, 0x8070); 	// DYNAMIC_SEQRAM_186
write_cmos_sensor_2byte(0x3E88, 0x0000); 	// DYNAMIC_SEQRAM_188
write_cmos_sensor_2byte(0x3E8A, 0x0000); 	// DYNAMIC_SEQRAM_18A
write_cmos_sensor_2byte(0x3E8C, 0x0000); 	// DYNAMIC_SEQRAM_18C
write_cmos_sensor_2byte(0x3E8E, 0x0000); 	// DYNAMIC_SEQRAM_18E
write_cmos_sensor_2byte(0x3E90, 0x0000); 	// DYNAMIC_SEQRAM_190
write_cmos_sensor_2byte(0x3E92, 0x0000); 	// DYNAMIC_SEQRAM_192
write_cmos_sensor_2byte(0x3E94, 0x0000); 	// DYNAMIC_SEQRAM_194
write_cmos_sensor_2byte(0x3E96, 0x0000); 	// DYNAMIC_SEQRAM_196
write_cmos_sensor_2byte(0x3E98, 0x0000); 	// DYNAMIC_SEQRAM_198
write_cmos_sensor_2byte(0x3E9A, 0x0000); 	// DYNAMIC_SEQRAM_19A
write_cmos_sensor_2byte(0x3E9C, 0x0000); 	// DYNAMIC_SEQRAM_19C
write_cmos_sensor_2byte(0x3E9E, 0x0000); 	// DYNAMIC_SEQRAM_19E
write_cmos_sensor_2byte(0x3EA0, 0x0000); 	// DYNAMIC_SEQRAM_1A0
write_cmos_sensor_2byte(0x3EA2, 0x0000); 	// DYNAMIC_SEQRAM_1A2
write_cmos_sensor_2byte(0x3EA4, 0x0000); 	// DYNAMIC_SEQRAM_1A4
write_cmos_sensor_2byte(0x3EA6, 0x0000); 	// DYNAMIC_SEQRAM_1A6
write_cmos_sensor_2byte(0x3EA8, 0x0000); 	// DYNAMIC_SEQRAM_1A8
write_cmos_sensor_2byte(0x3EAA, 0x0000); 	// DYNAMIC_SEQRAM_1AA
write_cmos_sensor_2byte(0x3EAC, 0x0000); 	// DYNAMIC_SEQRAM_1AC
write_cmos_sensor_2byte(0x3EAE, 0x0000); 	// DYNAMIC_SEQRAM_1AE
write_cmos_sensor_2byte(0x3EB0, 0x0000); 	// DYNAMIC_SEQRAM_1B0
write_cmos_sensor_2byte(0x3EB2, 0x0000); 	// DYNAMIC_SEQRAM_1B2
write_cmos_sensor_2byte(0x3EB4, 0x0000); 	// DYNAMIC_SEQRAM_1B4

////analog_setup_recommended in here
write_cmos_sensor_2byte(0x3EB6, 0x004D); 	// DAC_LD_0_1
write_cmos_sensor_2byte(0x3EB8, 0x010B); 	// DAC_LD_2_3
write_cmos_sensor_2byte(0x3EBC, 0xAA06); 	// DAC_LD_6_7
write_cmos_sensor_2byte(0x3EC0, 0x1E02); 	// DAC_LD_10_11
write_cmos_sensor_2byte(0x3EC2, 0x7700); 	// DAC_LD_12_13
write_cmos_sensor_2byte(0x3EC4, 0x1308); 	// DAC_LD_14_15
write_cmos_sensor_2byte(0x3EC6, 0xEA44); 	// DAC_LD_16_17
write_cmos_sensor_2byte(0x3EC8, 0x0F0F); 	// DAC_LD_18_19
write_cmos_sensor_2byte(0x3ECA, 0x0F4A); 	// DAC_LD_20_21
write_cmos_sensor_2byte(0x3ECC, 0x0706); 	// DAC_LD_22_23
write_cmos_sensor_2byte(0x3ECE, 0x443B); 	// DAC_LD_24_25
write_cmos_sensor_2byte(0x3ED0, 0x12F0); 	// DAC_LD_26_27
write_cmos_sensor_2byte(0x3ED2, 0x0039); 	// DAC_LD_28_29
write_cmos_sensor_2byte(0x3ED4, 0x862F); 	// DAC_LD_30_31
write_cmos_sensor_2byte(0x3ED6, 0x4080); 	// DAC_LD_32_33
write_cmos_sensor_2byte(0x3ED8, 0x0523); 	// DAC_LD_34_35
write_cmos_sensor_2byte(0x3EDA, 0xF896); 	// DAC_LD_36_37
write_cmos_sensor_2byte(0x3EDC, 0x508C); 	// DAC_LD_38_39  0x5096
write_cmos_sensor_2byte(0x3EDE, 0x5005); 	// DAC_LD_40_41
write_cmos_sensor_2byte(0x316A, 0x8200); 	// DAC_RSTLO
write_cmos_sensor_2byte(0x316E, 0x8200); 	// DAC_ECL
write_cmos_sensor_2byte(0x316C, 0x8200); 	// DAC_TXLO
write_cmos_sensor_2byte(0x3EF0, 0x414D); 	// DAC_LD_ECL 0x4d4d
write_cmos_sensor_2byte(0x3EF2, 0x0101); 	// DAC_LD_ECL2
write_cmos_sensor_2byte(0x3EF6, 0x0307); 	// DAC_LD_RSTLO2
write_cmos_sensor_2byte(0x3EFA, 0x0F0F); 	// DAC_LD_TXLO
write_cmos_sensor_2byte(0x3EFC, 0x0F0F); 	// DAC_LD_TXLO2
write_cmos_sensor_2byte(0x3EFE, 0x0F0F); 	// DAC_LD_TXLO3

// Defect_correction in here
write_cmos_sensor_2byte(0x30FE, 0x0060); 	// CTREE_OFF
write_cmos_sensor_2byte(0x31E0, 0x0781); 	// PIX_DEF_ID
write_cmos_sensor_2byte(0x3F00, 0x004F); 	// BM_T0
write_cmos_sensor_2byte(0x3F02, 0x0125); 	// BM_T1
write_cmos_sensor_2byte(0x3F04, 0x0020); 	// NOISE_GAIN_THRESHOLD0
write_cmos_sensor_2byte(0x3F06, 0x0040); 	// NOISE_GAIN_THRESHOLD1
write_cmos_sensor_2byte(0x3F08, 0x0070); 	// NOISE_GAIN_THRESHOLD2
write_cmos_sensor_2byte(0x3F0A, 0x0101); 	// NOISE_FLOOR10
write_cmos_sensor_2byte(0x3F0C, 0x0302); 	// NOISE_FLOOR32
write_cmos_sensor_2byte(0x3F1E, 0x0022); 	// NOISE_COEF
write_cmos_sensor_2byte(0x3F1A, 0x01FF); 	// CROSSFACTOR2
write_cmos_sensor_2byte(0x3F14, 0x0101); 	// SINGLE_K_FACTOR2
write_cmos_sensor_2byte(0x3F44, 0x0707); 	// COUPLE_K_FACTOR2
write_cmos_sensor_2byte(0x3F18, 0x011E); 	// CROSSFACTOR1
write_cmos_sensor_2byte(0x3F12, 0x0303); 	// SINGLE_K_FACTOR1
write_cmos_sensor_2byte(0x3F42, 0x1511); 	// COUPLE_K_FACTOR1
write_cmos_sensor_2byte(0x3F16, 0x011E); 	// CROSSFACTOR0
write_cmos_sensor_2byte(0x3F10, 0x0505); 	// SINGLE_K_FACTOR0
write_cmos_sensor_2byte(0x3F40, 0x1511); 	// COUPLE_K_FACTOR0


write_cmos_sensor_2byte(0x0300, 0x0004); 	// VT_PIX_CLK_DIV
write_cmos_sensor_2byte(0x0302, 0x0001); 	// VT_SYS_CLK_DIV
write_cmos_sensor_2byte(0x0304, 0x0303); 	// PRE_PLL_CLK_DIV
write_cmos_sensor_2byte(0x0306, 0x6E6E); 	// PLL_MULTIPLIER
write_cmos_sensor_2byte(0x0308, 0x000A); 	// OP_PIX_CLK_DIV
write_cmos_sensor_2byte(0x030A, 0x0001); 	// OP_SYS_CLK_DIV
write_cmos_sensor_2byte(0x0112, 0x0A0A); 	// CCP_DATA_FORMAT
write_cmos_sensor_2byte(0x3016, 0x0101); 	// ROW_SPEED
write_cmos_sensor_2byte(0x0344, 0x0010); 	// X_ADDR_START
write_cmos_sensor_2byte(0x0348, 0x107D); 	// X_ADDR_END
write_cmos_sensor_2byte(0x0346, 0x0010); 	// Y_ADDR_START
write_cmos_sensor_2byte(0x034A, 0x0C3D); 	// Y_ADDR_END
write_cmos_sensor_2byte(0x034C, 0x0838); 	// X_OUTPUT_SIZE
write_cmos_sensor_2byte(0x034E, 0x0618); 	// Y_OUTPUT_SIZE
write_cmos_sensor_2byte(0x3040, 0x0043); 	// READ_MODE
write_cmos_sensor_2byte(0x3172, 0x0206); 	// ANALOG_CONTROL2
write_cmos_sensor_2byte(0x317A, 0x516E); 	// ANALOG_CONTROL6
write_cmos_sensor_2byte(0x3F3C, 0x0003); 	// ANALOG_CONTROL9
write_cmos_sensor_2byte(0x0400, 0x0001); 	// SCALING_MODE
write_cmos_sensor_2byte(0x0404, 0x0020); 	// SCALE_M
write_cmos_sensor_2byte(0x0342, 0x1200); 	// LINE_LENGTH_PCK
write_cmos_sensor_2byte(0x0340, 0x0C6E); 	// FRAME_LENGTH_LINES
write_cmos_sensor_2byte(0x0202, 0x0C4F); 	// COARSE_INTEGRATION_TIME

write_cmos_sensor_2byte(0x31B0, 0x004D);   //Frame preamble 4D
write_cmos_sensor_2byte(0x31B2, 0x0028);   //Line preamble 28
write_cmos_sensor_2byte(0x31B4, 0x230E);   //MIPI timing0 230E
write_cmos_sensor_2byte(0x31B6, 0x1348);   //MIPI timing1 1348
write_cmos_sensor_2byte(0x31B8, 0x1C12);   //MIPI timing2 1C12
write_cmos_sensor_2byte(0x31BA, 0x185B);   //MIPI timing3 185B
write_cmos_sensor_2byte(0x31BC, 0x8509);   //MIPI timing4 8509
write_cmos_sensor_2byte(0x31AE, 0x0204);   //SERIAL_FORMAT


/// enable_streaming
write_cmos_sensor_2byte(0x3F3C, 0x0003); 	// ANALOG_CONTROL9
write_cmos_sensor_2byte(0x301A, 0x021C); 	// RESET_REGISTER

//// end init


#if 1
#if defined(AR1335_OTP)

        write_cmos_sensor_2byte(0x3780,0x8000);

        printk("init_setting after awb read 0x3780=0x%x\n",read_cmos_sensor_2(0x3780));
#endif
#endif
LOG_INF("sensor_init exit \n");
}    /*    sensor_init  */


static void preview_setting(void)
{
    //5.1.2 FQPreview AR1335_2456*1842_4Lane_30fps_Mclk24M_setting
 /// stop_streaming
write_cmos_sensor_2byte(0x3F3C, 0x0002);
write_cmos_sensor_2byte(0x3FE0, 0x0001);
write_cmos_sensor(0x0100, 0x00);      //mode_select	  8-bit
write_cmos_sensor_2byte(0x3FE0, 0x0000);
		mDELAY(20);
////vt_clk = 440mhz, dada rate 880mhz
write_cmos_sensor_2byte(0x0300, 0x0004); 	// VT_PIX_CLK_DIV
write_cmos_sensor_2byte(0x0302, 0x0001); 	// VT_SYS_CLK_DIV
write_cmos_sensor_2byte(0x0304, 0x0303); 	// PRE_PLL_CLK_DIV
write_cmos_sensor_2byte(0x0306, 0x6E6E); 	// PLL_MULTIPLIER
write_cmos_sensor_2byte(0x0308, 0x000A); 	// OP_PIX_CLK_DIV
write_cmos_sensor_2byte(0x030A, 0x0001); 	// OP_SYS_CLK_DIV
write_cmos_sensor_2byte(0x0112, 0x0A0A); 	// CCP_DATA_FORMAT
write_cmos_sensor_2byte(0x3016, 0x0101); 	// ROW_SPEED
write_cmos_sensor_2byte(0x0344, 0x0010); 	// X_ADDR_START
write_cmos_sensor_2byte(0x0348, 0x107D); 	// X_ADDR_END
write_cmos_sensor_2byte(0x0346, 0x0010); 	// Y_ADDR_START
write_cmos_sensor_2byte(0x034A, 0x0C3D); 	// Y_ADDR_END
write_cmos_sensor_2byte(0x034C, 0x0838); 	// X_OUTPUT_SIZE
write_cmos_sensor_2byte(0x034E, 0x0618); 	// Y_OUTPUT_SIZE
write_cmos_sensor_2byte(0x3040, 0x0043); 	// READ_MODE
write_cmos_sensor_2byte(0x3172, 0x0206); 	// ANALOG_CONTROL2
write_cmos_sensor_2byte(0x317A, 0x516E); 	// ANALOG_CONTROL6
write_cmos_sensor_2byte(0x3F3C, 0x0003); 	// ANALOG_CONTROL9
write_cmos_sensor_2byte(0x0400, 0x0001); 	// SCALING_MODE
write_cmos_sensor_2byte(0x0404, 0x0020); 	// SCALE_M
write_cmos_sensor_2byte(0x0342, 0x1200); 	// LINE_LENGTH_PCK
write_cmos_sensor_2byte(0x0340, 0x0C6E); 	// FRAME_LENGTH_LINES
write_cmos_sensor_2byte(0x0202, 0x0C4F); 	// COARSE_INTEGRATION_TIME

write_cmos_sensor_2byte(0x31B0, 0x004D);   //Frame preamble 4D
write_cmos_sensor_2byte(0x31B2, 0x0028);   //Line preamble 28
write_cmos_sensor_2byte(0x31B4, 0x230E);   //MIPI timing0 230E
write_cmos_sensor_2byte(0x31B6, 0x1348);   //MIPI timing1 1348
write_cmos_sensor_2byte(0x31B8, 0x1C12);   //MIPI timing2 1C12
write_cmos_sensor_2byte(0x31BA, 0x185B);   //MIPI timing3 185B
write_cmos_sensor_2byte(0x31BC, 0x8509);   //MIPI timing4 8509
write_cmos_sensor_2byte(0x31AE, 0x0204);   //SERIAL_FORMAT

write_cmos_sensor_2byte(0x3F3C, 0x0003);
write_cmos_sensor(0x0100, 0x01);      //bit 8//mode_select  8-bit
	mDELAY(20);

}    /*    preview_setting  */

static void capture_setting(kal_uint16 currefps)
{
    LOG_INF("E! currefps:%d\n",currefps);
  /// stop_streaming
write_cmos_sensor_2byte(0x3F3C, 0x0002);
write_cmos_sensor_2byte(0x3FE0, 0x0001);
write_cmos_sensor(0x0100, 0x00);     //mode_select	   8bit
write_cmos_sensor_2byte(0x3FE0, 0x0000);
		mDELAY(20);

/// Mclk = 24Mhz , vt_clk = 441.6Mhz
////// vt_clk = 441.6mhz pclk = 110.4mhz data rate = 1104mhz  4208*3120@30fps
write_cmos_sensor_2byte(0x31AE, 0x0204); 	// SERIAL_FORMAT
write_cmos_sensor_2byte(0x0300, 0x0004); 	// VT_PIX_CLK_DIV
write_cmos_sensor_2byte(0x0302, 0x0001); 	// VT_SYS_CLK_DIV
write_cmos_sensor_2byte(0x0304, 0x0303); 	// PRE_PLL_CLK_DIV
write_cmos_sensor_2byte(0x0306, 0x6E6E); 	// PLL_MULTIPLIER
write_cmos_sensor_2byte(0x0308, 0x000A); 	// OP_PIX_CLK_DIV
write_cmos_sensor_2byte(0x030A, 0x0001); 	// OP_SYS_CLK_DIV
write_cmos_sensor_2byte(0x0112, 0x0A0A); 	// CCP_DATA_FORMAT
write_cmos_sensor_2byte(0x3016, 0x0101); 	// ROW_SPEED
write_cmos_sensor_2byte(0x0344, 0x0010); 	// X_ADDR_START
write_cmos_sensor_2byte(0x0348, 0x107F); 	// X_ADDR_END
write_cmos_sensor_2byte(0x0346, 0x0010); 	// Y_ADDR_START
write_cmos_sensor_2byte(0x034A, 0x0C3F); 	// Y_ADDR_END
write_cmos_sensor_2byte(0x034C, 0x1070); 	// X_OUTPUT_SIZE
write_cmos_sensor_2byte(0x034E, 0x0C30); 	// Y_OUTPUT_SIZE
write_cmos_sensor_2byte(0x3040, 0x0041); 	// READ_MODE
write_cmos_sensor_2byte(0x0112, 0x0A0A); 	// CCP_DATA_FORMAT
write_cmos_sensor_2byte(0x0112, 0x0A0A); 	// CCP_DATA_FORMAT
write_cmos_sensor_2byte(0x3172, 0x0206); 	// ANALOG_CONTROL2
write_cmos_sensor_2byte(0x317A, 0x416E); 	// ANALOG_CONTROL6
write_cmos_sensor_2byte(0x3F3C, 0x0003); 	// ANALOG_CONTROL9

write_cmos_sensor_2byte(0x0400, 0x0000); 	// SCALING_MODE
write_cmos_sensor_2byte(0x0404, 0x0010); 	// SCALE_M
write_cmos_sensor_2byte(0x0342, 0x165E); 	// LINE_LENGTH_PCK
write_cmos_sensor_2byte(0x0340, 0x0C80); 	// FRAME_LENGTH_LINES
write_cmos_sensor_2byte(0x0202, 0x0C7A); 	// COARSE_INTEGRATION_TIME
/// MIPI_TIMING in here
write_cmos_sensor_2byte(0x31B0, 0x004D);    //Frame preamble 5C
write_cmos_sensor_2byte(0x31B2, 0x0028);   //Line preamble 2D
write_cmos_sensor_2byte(0x31B4, 0x230E);   //MIPI timing0 2412
write_cmos_sensor_2byte(0x31B6, 0x1348);   //MIPI timing1 142A
write_cmos_sensor_2byte(0x31B8, 0x1C12);   //MIPI timing2 2413
write_cmos_sensor_2byte(0x31BA, 0x185B);   //MIPI timing3 1C70
write_cmos_sensor_2byte(0x31BC, 0x8509);   //MIPI timing4 868B
write_cmos_sensor_2byte(0x31AE, 0x0204);    //SERIAL_FORMAT
//start streaming
write_cmos_sensor_2byte(0x3F3C, 0x0003);
write_cmos_sensor(0x0100, 0x01);      //mode_select  8-bit


    mDELAY(20);

}

static void normal_video_setting(kal_uint16 currefps)
{
	/*
    LOG_INF("E! currefps:%d\n",currefps);

  	/// stop_streaming
	write_cmos_sensor_2byte(0x3F3C, 0x0002);
	write_cmos_sensor_2byte(0x3FE0, 0x0001);
	write_cmos_sensor(0x0100, 0x00); //mode_select	 8-bit
	write_cmos_sensor_2byte(0x3FE0, 0x0000);
		mDELAY(50);
	////// vt_clk = 441.6mhz pclk = 110.4mhz data rate = 1104mhz  3840*2160@30fps
	write_cmos_sensor_2byte(0x0300, 0x0005); 	// VT_PIX_CLK_DIV
	write_cmos_sensor_2byte(0x0302, 0x0001); 	// VT_SYS_CLK_DIV
	write_cmos_sensor_2byte(0x0304, 0x0101); 	// PRE_PLL_CLK_DIV
	write_cmos_sensor_2byte(0x0306, 0x2E2E); 	// PLL_MULTIPLIER
	write_cmos_sensor_2byte(0x0308, 0x000A); 	// OP_PIX_CLK_DIV
	write_cmos_sensor_2byte(0x030A, 0x0001); 	// OP_SYS_CLK_DIV
	write_cmos_sensor_2byte(0x0112, 0x0A0A); 	// CCP_DATA_FORMAT
	write_cmos_sensor_2byte(0x3016, 0x0101); 	// ROW_SPEED
	write_cmos_sensor_2byte(0x0344, 0x00C8); 	//X_ADDR_START 200
	write_cmos_sensor_2byte(0x0348, 0x0FC7); 	//X_ADDR_END 4039
	write_cmos_sensor_2byte(0x0346, 0x01F0); 	//Y_ADDR_START 496
	write_cmos_sensor_2byte(0x034A, 0x0A5F); 	//Y_ADDR_END 2359
	write_cmos_sensor_2byte(0x034C, 0x0F00); 	//X_OUTPUT_SIZE 3840
	write_cmos_sensor_2byte(0x034E, 0x0870); 	//Y_OUTPUT_SIZE 2160
	write_cmos_sensor_2byte(0x3040, 0x0041); 	//read_mode
	write_cmos_sensor_2byte(0x3172, 0x0206); 	//digbin_enable
	write_cmos_sensor_2byte(0x317A, 0x416E); 	//sfbin_enable
	write_cmos_sensor_2byte(0x3F3C, 0x0003); 	//bin4
	write_cmos_sensor_2byte(0x0400, 0x0000); 	// SCALING_MODE
	write_cmos_sensor_2byte(0x0404, 0x0010); 	// SCALE_M
	write_cmos_sensor_2byte(0x0342, 0x1200);		//LINE_LENGTH_PCK 2304
	write_cmos_sensor_2byte(0x0340, 0x0C7A);		//FRAME_LENGTH_LINES 3194
	write_cmos_sensor_2byte(0x0202, 0x0C5A); 	//COARSE_INTEGRATION_TIME 3162
	/// MIPI_TIMING in here
	write_cmos_sensor_2byte(0x31B0, 0x005C);   //Frame preamble 5C
	write_cmos_sensor_2byte(0x31B2, 0x002D);   //Line preamble 2D
	write_cmos_sensor_2byte(0x31B4, 0x2412);   //MIPI timing0 2412
	write_cmos_sensor_2byte(0x31B6, 0x142A);   //MIPI timing1 142A
	write_cmos_sensor_2byte(0x31B8, 0x2413);   //MIPI timing2 2413
	write_cmos_sensor_2byte(0x31BA, 0x1C70);   //MIPI timing3 1C70
	write_cmos_sensor_2byte(0x31BC, 0x868B);   //MIPI timing4 868B
	write_cmos_sensor_2byte(0x31AE, 0x0204);   //SERIAL_FORMAT

	write_cmos_sensor_2byte(0x3F3C, 0x0003);
	write_cmos_sensor(0x0100, 0x01); //bit 8//mode_select 8-bit
    msleep(50);
    */
		// 1080p_30fps
														LOG_INF("E\---hs_video 1080p 30fps \n");
write_cmos_sensor_2byte(0x3F3C, 0x0002);
write_cmos_sensor_2byte(0x3FE0, 0x0001);
write_cmos_sensor(0x0100, 0x00); //mode_select	 8-bit
write_cmos_sensor_2byte(0x3FE0, 0x0000);
																	mDELAY(20);
////// vt_clk = 441.6mhz pclk = 110.4mhz data rate = 1104mhz  3840*2160@30fps
													write_cmos_sensor_2byte(0x0300, 0x0004);	  // VT_PIX_CLK_DIV
write_cmos_sensor_2byte(0x0302, 0x0001); 	// VT_SYS_CLK_DIV
													write_cmos_sensor_2byte(0x0304, 0x0303);	  // PRE_PLL_CLK_DIV
													write_cmos_sensor_2byte(0x0306, 0x6E6E);	  // PLL_MULTIPLIER
write_cmos_sensor_2byte(0x0308, 0x000A); 	// OP_PIX_CLK_DIV
write_cmos_sensor_2byte(0x030A, 0x0001); 	// OP_SYS_CLK_DIV
write_cmos_sensor_2byte(0x0112, 0x0A0A); 	// CCP_DATA_FORMAT
write_cmos_sensor_2byte(0x3016, 0x0101); 	// ROW_SPEED
	write_cmos_sensor_2byte(0x0344, 0x0010);	  // X_ADDR_START
	write_cmos_sensor_2byte(0x0348, 0x107D);	  // X_ADDR_END
write_cmos_sensor_2byte(0x0346, 0x01F0); 	//Y_ADDR_START 496
													write_cmos_sensor_2byte(0x034A, 0x0A5D);	// Y_ADDR_END
													write_cmos_sensor_2byte(0x034C, 0x0780);	  // X_OUTPUT_SIZE
													write_cmos_sensor_2byte(0x034E, 0x0438);	  // Y_OUTPUT_SIZE
													write_cmos_sensor_2byte(0x3040, 0x0043);	  // READ_MODE
write_cmos_sensor_2byte(0x3172, 0x0206); 	//digbin_enable
													write_cmos_sensor_2byte(0x317A, 0x516E);	  // ANALOG_CONTROL6
write_cmos_sensor_2byte(0x3F3C, 0x0003); 	//bin4
													write_cmos_sensor_2byte(0x0400, 0x0001);	  // SCALING_MODE
	write_cmos_sensor_2byte(0x0404, 0x0023);	  // SCALE_M
write_cmos_sensor_2byte(0x0342, 0x1200);		//LINE_LENGTH_PCK 2304
													write_cmos_sensor_2byte(0x0340, 0x0C6E);	  // FRAME_LENGTH_LINES
													write_cmos_sensor_2byte(0x0202, 0x0C6A);	 // COARSE_INTEGRATION_TIMEE
/// MIPI_TIMING in here
													write_cmos_sensor_2byte(0x31B0, 0x004D);   //Frame preamble 4D
													write_cmos_sensor_2byte(0x31B2, 0x0028);   //Line preamble 28
													write_cmos_sensor_2byte(0x31B4, 0x230E);   //MIPI timing0 230E
													write_cmos_sensor_2byte(0x31B6, 0x1348);   //MIPI timing1 1348
													write_cmos_sensor_2byte(0x31B8, 0x1C12);   //MIPI timing2 1C12
													write_cmos_sensor_2byte(0x31BA, 0x185B);   //MIPI timing3 185B
													write_cmos_sensor_2byte(0x31BC, 0x8509);   //MIPI timing4 8509
write_cmos_sensor_2byte(0x31AE, 0x0204);   //SERIAL_FORMAT

write_cmos_sensor_2byte(0x3F3C, 0x0003);
write_cmos_sensor(0x0100, 0x01); //bit 8//mode_select 8-bit
																	mDELAY(20);

}
static void hs_video_setting()
{
//720p_120fps
    LOG_INF("E hs_video  120fps \n");
   /// stop_streaming
write_cmos_sensor_2byte(0x3F3C, 0x0002);
write_cmos_sensor_2byte(0x3FE0, 0x0001);
write_cmos_sensor(0x0100, 0x00  );   //mode_select	 8-bit
write_cmos_sensor_2byte(0x3FE0, 0x0000);
		mDELAY(20);
//720p_array_setup_3840_2160_Yskip3_XScale3
////vt_clk = 440mhz, dada rate 880mhz
write_cmos_sensor_2byte(0x0300, 0x0004);	// VT_PIX_CLK_DIV
write_cmos_sensor_2byte(0x0302, 0x0001);	// VT_SYS_CLK_DIV
write_cmos_sensor_2byte(0x0304, 0x0303);	// PRE_PLL_CLK_DIV
write_cmos_sensor_2byte(0x0306, 0x6E6E);	// PLL_MULTIPLIER
write_cmos_sensor_2byte(0x0308, 0x000A);	// OP_PIX_CLK_DIV
write_cmos_sensor_2byte(0x030A, 0x0001);	// OP_SYS_CLK_DIV
write_cmos_sensor_2byte(0x0112, 0x0A0A);	// CCP_DATA_FORMAT
write_cmos_sensor_2byte(0x3016, 0x0101);	// ROW_SPEED
write_cmos_sensor_2byte(0x0344, 0x00C8);	// X_ADDR_START
write_cmos_sensor_2byte(0x0348, 0x0FC7);	// X_ADDR_END
write_cmos_sensor_2byte(0x0346, 0x01F0);	// Y_ADDR_START
write_cmos_sensor_2byte(0x034A, 0x0A5B);	// Y_ADDR_END
write_cmos_sensor_2byte(0x034C, 0x0500);	// X_OUTPUT_SIZE
write_cmos_sensor_2byte(0x034E, 0x02D0);	// Y_OUTPUT_SIZE
write_cmos_sensor_2byte(0x3040, 0x0045);	// READ_MODE
write_cmos_sensor_2byte(0x3172, 0x0206);	// ANALOG_CONTROL2
write_cmos_sensor_2byte(0x317A, 0x516E);	// ANALOG_CONTROL6
write_cmos_sensor_2byte(0x3F3C, 0x0003);	// ANALOG_CONTROL9
write_cmos_sensor_2byte(0x0400, 0x0001);	// SCALING_MODE
write_cmos_sensor_2byte(0x0404, 0x0030);	// SCALE_M
write_cmos_sensor_2byte(0x0342, 0x1200);	// LINE_LENGTH_PCK
write_cmos_sensor_2byte(0x0340, 0x031A);	// FRAME_LENGTH_LINES
write_cmos_sensor_2byte(0x0202, 0x033c);	// COARSE_INTEGRATION_TIME
write_cmos_sensor_2byte(0x31B0, 0x004D);  //Frame preamble 4D
write_cmos_sensor_2byte(0x31B2, 0x0028);  //Line preamble 28
write_cmos_sensor_2byte(0x31B4, 0x230E);  //MIPI timing0 230E
write_cmos_sensor_2byte(0x31B6, 0x1348);  //MIPI timing1 1348
write_cmos_sensor_2byte(0x31B8, 0x1C12);  //MIPI timing2 1C12
write_cmos_sensor_2byte(0x31BA, 0x185B);  //MIPI timing3 185B
write_cmos_sensor_2byte(0x31BC, 0x8509);  //MIPI timing4 8509
write_cmos_sensor_2byte(0x31AE, 0x0204);  //SERIAL_FORMAT

write_cmos_sensor_2byte(0x3F3C, 0x0003);
write_cmos_sensor(0x0100, 0x01 	);		//bit 8//mode_select  8-bit
	mDELAY(20);

    //msleep(100);
}

static void slim_video_setting()
{
	//5.1.2 FQPreview AR1335_2456*1842_4Lane_30fps_Mclk24M_setting
 /// stop_streaming
	write_cmos_sensor_2byte(0x3F3C, 0x0002);
	write_cmos_sensor_2byte(0x3FE0, 0x0001);
	write_cmos_sensor(0x0100, 0x00);      //mode_select	  8-bit
	write_cmos_sensor_2byte(0x3FE0, 0x0000);
		mDELAY(20);

			//1080p_array_setup_3840_2160_Ybin2_Xscale2
			////vt_clk = 440mhz, dada rate 880mhz
			write_cmos_sensor_2byte(0x0300, 0x0004); 	// VT_PIX_CLK_DIV
			write_cmos_sensor_2byte(0x0302, 0x0001); 	// VT_SYS_CLK_DIV
			write_cmos_sensor_2byte(0x0304, 0x0303); 	// PRE_PLL_CLK_DIV
			write_cmos_sensor_2byte(0x0306, 0x6E6E); 	// PLL_MULTIPLIER
			write_cmos_sensor_2byte(0x0308, 0x000A); 	// OP_PIX_CLK_DIV
			write_cmos_sensor_2byte(0x030A, 0x0001); 	// OP_SYS_CLK_DIV
			write_cmos_sensor_2byte(0x0112, 0x0A0A); 	// CCP_DATA_FORMAT
			write_cmos_sensor_2byte(0x3016, 0x0101); 	// ROW_SPEED
	write_cmos_sensor_2byte(0x0344, 0x0010); 	// X_ADDR_START
	write_cmos_sensor_2byte(0x0348, 0x107D); 	// X_ADDR_END
	write_cmos_sensor_2byte(0x0346, 0x0010); 	// Y_ADDR_START
	write_cmos_sensor_2byte(0x034A, 0x0C3D); 	// Y_ADDR_END
	write_cmos_sensor_2byte(0x034C, 0x0838); 	// X_OUTPUT_SIZE
	write_cmos_sensor_2byte(0x034E, 0x0618); 	// Y_OUTPUT_SIZE
			write_cmos_sensor_2byte(0x3040, 0x0043); 	// READ_MODE
			write_cmos_sensor_2byte(0x3172, 0x0206); 	// ANALOG_CONTROL2
			write_cmos_sensor_2byte(0x317A, 0x516E); 	// ANALOG_CONTROL6
			write_cmos_sensor_2byte(0x3F3C, 0x0003); 	// ANALOG_CONTROL9
			write_cmos_sensor_2byte(0x0400, 0x0001); 	// SCALING_MODE
			write_cmos_sensor_2byte(0x0404, 0x0020); 	// SCALE_M
			write_cmos_sensor_2byte(0x0342, 0x1200); 	// LINE_LENGTH_PCK
	write_cmos_sensor_2byte(0x0340, 0x0C6E); 	// FRAME_LENGTH_LINES
	write_cmos_sensor_2byte(0x0202, 0x0C4F); 	// COARSE_INTEGRATION_TIME

			write_cmos_sensor_2byte(0x31B0, 0x004D);   //Frame preamble 4D
			write_cmos_sensor_2byte(0x31B2, 0x0028);   //Line preamble 28
			write_cmos_sensor_2byte(0x31B4, 0x230E);   //MIPI timing0 230E
			write_cmos_sensor_2byte(0x31B6, 0x1348);   //MIPI timing1 1348
			write_cmos_sensor_2byte(0x31B8, 0x1C12);   //MIPI timing2 1C12
			write_cmos_sensor_2byte(0x31BA, 0x185B);   //MIPI timing3 185B
			write_cmos_sensor_2byte(0x31BC, 0x8509);   //MIPI timing4 8509
			write_cmos_sensor_2byte(0x31AE, 0x0204);   //SERIAL_FORMAT

			write_cmos_sensor_2byte(0x3F3C, 0x0003);
			write_cmos_sensor(0x0100, 0x01); 			//bit 8//mode_select  8-bit
	mDELAY(20);
}

static kal_uint32 set_test_pattern_mode(kal_bool enable)
{
    LOG_INF("enable: %d\n", enable);

    if (enable) {
        // 0x5E00[8]: 1 enable,  0 disable
        // 0x5E00[1:0]; 00 Color bar, 01 Random Data, 10 Square, 11 BLACK
        write_cmos_sensor_2byte(0x3070, 0x0002);
    } else {
        // 0x5E00[8]: 1 enable,  0 disable
        // 0x5E00[1:0]; 00 Color bar, 01 Random Data, 10 Square, 11 BLACK
        write_cmos_sensor_2byte(0x3070, 0x0000);
    }
    spin_lock(&imgsensor_drv_lock);
    imgsensor.test_pattern = enable;
    spin_unlock(&imgsensor_drv_lock);
    return ERROR_NONE;
}

/*************************************************************************
* FUNCTION
*    get_imgsensor_id
*
* DESCRIPTION
*    This function get the sensor ID
*
* PARAMETERS
*    *sensorID : return the sensor ID
*
* RETURNS
*    None
*
* GLOBALS AFFECTED
*
*************************************************************************/
static kal_uint32 get_imgsensor_id(UINT32 *sensor_id)
{
    kal_uint8 i = 0;
    kal_uint8 retry = 2;
    kal_uint8 sensor_moudle_id =0;
    //sensor have two i2c address 0x6c 0x6d & 0x21 0x20, we should detect the module used i2c address
    while (imgsensor_info.i2c_addr_table[i] != 0xff) {
        spin_lock(&imgsensor_drv_lock);
        imgsensor.i2c_write_id = imgsensor_info.i2c_addr_table[i];
        spin_unlock(&imgsensor_drv_lock);
        do {
            *sensor_id = return_sensor_id();
            if (*sensor_id == imgsensor_info.sensor_id) {
                LOG_INF("i2c write id: 0x%x, sensor id: 0x%x\n", imgsensor.i2c_write_id,*sensor_id);
                break;
                //return ERROR_NONE;
            }
            LOG_INF("Read sensor id fail, write id: 0x%x, id: 0x%x\n", imgsensor.i2c_write_id,*sensor_id);
            retry--;
        } while(retry > 0);
        i++;
        retry = 2;
    }
    if (*sensor_id != imgsensor_info.sensor_id) {
        // if Sensor ID is not correct, Must set *sensor_id to 0xFFFFFFFF
        *sensor_id = 0xFFFFFFFF;
        return ERROR_SENSOR_CONNECT_FAIL;
    }

    AR1335_otp_read(0x30);
    sensor_moudle_id = read_cmos_sensor_2(0x3802);
    LOG_INF("Read sensor_moudle_id : 0x%x, sensor_id: 0x%x\n", sensor_moudle_id,*sensor_id);
    if(AR1335_QTECH_MODULE_ID == sensor_moudle_id)
    {
        RG_Ratio_Typical = 0x229;
        BG_Ratio_Typical = 0x241;
        GG_Ratio_Typical = 0x400;
        LOG_INF("Read sensor_moudle_id == ar1335 qtech\n");
        *sensor_id = AR1335_QTECH_SENSOR_ID;
        sprintf(factory_module_id,"main_camera:13M-Camera ar1335-qtech\n");     //display camera device info by miaolei@yulong.com 2015.05.25
    }
    else
    {
        *sensor_id = 0xffffffff;
        LOG_INF("Read sensor_moudle_id ==  unknown\n");
        return ERROR_SENSOR_CONNECT_FAIL;
    }

    if(info_limit<1){
        LOG_INF("sinfo_limit<1\n");
        get_device_info(factory_module_id);    //display camera device info by miaolei@yulong.com 2015.05.19
    }
   // AR1335_OTP_AUTO_LOAD_LSC();
    AR1335_otp_read_awb(); //gaoatao modify  error 2015-4-4 20:33:06
    info_limit++;
    return ERROR_NONE;
}


/*************************************************************************
* FUNCTION
*    open
*
* DESCRIPTION
*    This function initialize the registers of CMOS sensor
*
* PARAMETERS
*    None
*
* RETURNS
*    None
*
* GLOBALS AFFECTED
*
*************************************************************************/
static kal_uint32 open(void)
{
    kal_uint8 i = 0;
    kal_uint8 retry = 2;
    kal_uint32 sensor_id = 0;
    LOG_1;
    LOG_2;
    //sensor have two i2c address 0x6c 0x6d & 0x21 0x20, we should detect the module used i2c address
    while (imgsensor_info.i2c_addr_table[i] != 0xff) {
        spin_lock(&imgsensor_drv_lock);
        imgsensor.i2c_write_id = imgsensor_info.i2c_addr_table[i];
        spin_unlock(&imgsensor_drv_lock);
        do {
            sensor_id = return_sensor_id();
            if (sensor_id == imgsensor_info.sensor_id) {
                LOG_INF("i2c write id: 0x%x, sensor id: 0x%x\n", imgsensor.i2c_write_id,sensor_id);
                break;
            }
            LOG_INF("Read sensor id fail, write id: 0x%x, id: 0x%x\n", imgsensor.i2c_write_id,sensor_id);
            retry--;
        } while(retry > 0);
        i++;
        if (sensor_id == imgsensor_info.sensor_id)
            break;
        retry = 2;
    }
    if (imgsensor_info.sensor_id != sensor_id)
        return ERROR_SENSOR_CONNECT_FAIL;

    /* initail sequence write in  */
    sensor_init();

    spin_lock(&imgsensor_drv_lock);

    imgsensor.autoflicker_en= KAL_FALSE;
    imgsensor.sensor_mode = IMGSENSOR_MODE_INIT;
    imgsensor.pclk = imgsensor_info.pre.pclk;
    imgsensor.frame_length = imgsensor_info.pre.framelength;
    imgsensor.line_length = imgsensor_info.pre.linelength;
    imgsensor.min_frame_length = imgsensor_info.pre.framelength;
    imgsensor.dummy_pixel = 0;
    imgsensor.dummy_line = 0;
    imgsensor.ihdr_en = 0;
    imgsensor.test_pattern = KAL_FALSE;
    imgsensor.current_fps = imgsensor_info.pre.max_framerate;
    spin_unlock(&imgsensor_drv_lock);

    return ERROR_NONE;
}    /*    open  */



/*************************************************************************
* FUNCTION
*    close
*
* DESCRIPTION
*
*
* PARAMETERS
*    None
*
* RETURNS
*    None
*
* GLOBALS AFFECTED
*
*************************************************************************/
static kal_uint32 close(void)
{
    LOG_INF("E\n");

    /*No Need to implement this function*/

    return ERROR_NONE;
}    /*    close  */


/*************************************************************************
* FUNCTION
* preview
*
* DESCRIPTION
*    This function start the sensor preview.
*
* PARAMETERS
*    *image_window : address pointer of pixel numbers in one period of HSYNC
*  *sensor_config_data : address pointer of line numbers in one period of VSYNC
*
* RETURNS
*    None
*
* GLOBALS AFFECTED
*
*************************************************************************/
static kal_uint32 preview(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
                      MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
    LOG_INF("E\n");

    spin_lock(&imgsensor_drv_lock);
    imgsensor.sensor_mode = IMGSENSOR_MODE_PREVIEW;
    imgsensor.pclk = imgsensor_info.pre.pclk;
    //imgsensor.video_mode = KAL_FALSE;
    imgsensor.line_length = imgsensor_info.pre.linelength;
    imgsensor.frame_length = imgsensor_info.pre.framelength;
    imgsensor.min_frame_length = imgsensor_info.pre.framelength;
    imgsensor.autoflicker_en = KAL_FALSE;
    spin_unlock(&imgsensor_drv_lock);
    preview_setting();
	set_mirror_flip(imgsensor.mirror);
    return ERROR_NONE;
}    /*    preview   */

/*************************************************************************
* FUNCTION
*    capture
*
* DESCRIPTION
*    This function setup the CMOS sensor in capture MY_OUTPUT mode
*
* PARAMETERS
*
* RETURNS
*    None
*
* GLOBALS AFFECTED
*
*************************************************************************/
static kal_uint32 capture(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
                          MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
    LOG_INF("E\n");

    spin_lock(&imgsensor_drv_lock);
    imgsensor.sensor_mode = IMGSENSOR_MODE_CAPTURE;
    if (imgsensor.current_fps == imgsensor_info.cap1.max_framerate) {//PIP capture: 24fps for less than 13M, 20fps for 16M,15fps for 20M
        imgsensor.pclk = imgsensor_info.cap1.pclk;
        imgsensor.line_length = imgsensor_info.cap1.linelength;
        imgsensor.frame_length = imgsensor_info.cap1.framelength;
        imgsensor.min_frame_length = imgsensor_info.cap1.framelength;
        imgsensor.autoflicker_en = KAL_FALSE;
    } else {
        if (imgsensor.current_fps != imgsensor_info.cap.max_framerate)
            LOG_INF("Warning: current_fps %d fps is not support, so use cap's setting: %d fps!\n",imgsensor.current_fps,imgsensor_info.cap.max_framerate/10);
        imgsensor.pclk = imgsensor_info.cap.pclk;
        imgsensor.line_length = imgsensor_info.cap.linelength;
        imgsensor.frame_length = imgsensor_info.cap.framelength;
        imgsensor.min_frame_length = imgsensor_info.cap.framelength;
        imgsensor.autoflicker_en = KAL_FALSE;
    }
    spin_unlock(&imgsensor_drv_lock);
    capture_setting(imgsensor.current_fps);
	set_mirror_flip(imgsensor.mirror);
    return ERROR_NONE;
}    /* capture() */
static kal_uint32 normal_video(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
                      MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
    LOG_INF("E\n");

    spin_lock(&imgsensor_drv_lock);
    imgsensor.sensor_mode = IMGSENSOR_MODE_VIDEO;
    imgsensor.pclk = imgsensor_info.normal_video.pclk;
    imgsensor.line_length = imgsensor_info.normal_video.linelength;
    imgsensor.frame_length = imgsensor_info.normal_video.framelength;
    imgsensor.min_frame_length = imgsensor_info.normal_video.framelength;
    //imgsensor.current_fps = 300;
    imgsensor.autoflicker_en = KAL_FALSE;
    spin_unlock(&imgsensor_drv_lock);
    normal_video_setting(imgsensor.current_fps);
	set_mirror_flip(imgsensor.mirror);
    return ERROR_NONE;
}    /*    normal_video   */

static kal_uint32 hs_video(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
                      MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
    LOG_INF("E\n");

    spin_lock(&imgsensor_drv_lock);
    imgsensor.sensor_mode = IMGSENSOR_MODE_HIGH_SPEED_VIDEO;
    imgsensor.pclk = imgsensor_info.hs_video.pclk;
    //imgsensor.video_mode = KAL_TRUE;
    imgsensor.line_length = imgsensor_info.hs_video.linelength;
    imgsensor.frame_length = imgsensor_info.hs_video.framelength;
    imgsensor.min_frame_length = imgsensor_info.hs_video.framelength;
    imgsensor.dummy_line = 0;
    imgsensor.dummy_pixel = 0;
    imgsensor.autoflicker_en = KAL_FALSE;
    spin_unlock(&imgsensor_drv_lock);
    hs_video_setting();
	set_mirror_flip(imgsensor.mirror);
    return ERROR_NONE;
}    /*    hs_video   */

static kal_uint32 slim_video(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
                      MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
    LOG_INF("E\n");

    spin_lock(&imgsensor_drv_lock);
    imgsensor.sensor_mode = IMGSENSOR_MODE_SLIM_VIDEO;
    imgsensor.pclk = imgsensor_info.slim_video.pclk;
    imgsensor.line_length = imgsensor_info.slim_video.linelength;
    imgsensor.frame_length = imgsensor_info.slim_video.framelength;
    imgsensor.min_frame_length = imgsensor_info.slim_video.framelength;
    imgsensor.dummy_line = 0;
    imgsensor.dummy_pixel = 0;
    imgsensor.autoflicker_en = KAL_FALSE;
    spin_unlock(&imgsensor_drv_lock);
    slim_video_setting();
	set_mirror_flip(imgsensor.mirror);

    return ERROR_NONE;
}    /*    slim_video     */



static kal_uint32 get_resolution(MSDK_SENSOR_RESOLUTION_INFO_STRUCT *sensor_resolution)
{
    LOG_INF("E\n");
    sensor_resolution->SensorFullWidth = imgsensor_info.cap.grabwindow_width;
    sensor_resolution->SensorFullHeight = imgsensor_info.cap.grabwindow_height;

    sensor_resolution->SensorPreviewWidth = imgsensor_info.pre.grabwindow_width;
    sensor_resolution->SensorPreviewHeight = imgsensor_info.pre.grabwindow_height;

    sensor_resolution->SensorVideoWidth = imgsensor_info.normal_video.grabwindow_width;
    sensor_resolution->SensorVideoHeight = imgsensor_info.normal_video.grabwindow_height;


    sensor_resolution->SensorHighSpeedVideoWidth     = imgsensor_info.hs_video.grabwindow_width;
    sensor_resolution->SensorHighSpeedVideoHeight     = imgsensor_info.hs_video.grabwindow_height;

    sensor_resolution->SensorSlimVideoWidth     = imgsensor_info.slim_video.grabwindow_width;
    sensor_resolution->SensorSlimVideoHeight     = imgsensor_info.slim_video.grabwindow_height;
    return ERROR_NONE;
}    /*    get_resolution    */

static kal_uint32 get_info(MSDK_SCENARIO_ID_ENUM scenario_id,
                      MSDK_SENSOR_INFO_STRUCT *sensor_info,
                      MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
    LOG_INF("scenario_id = %d\n", scenario_id);


    //sensor_info->SensorVideoFrameRate = imgsensor_info.normal_video.max_framerate/10; /* not use */
    //sensor_info->SensorStillCaptureFrameRate= imgsensor_info.cap.max_framerate/10; /* not use */
    //imgsensor_info->SensorWebCamCaptureFrameRate= imgsensor_info.v.max_framerate; /* not use */

    sensor_info->SensorClockPolarity = SENSOR_CLOCK_POLARITY_LOW;
    sensor_info->SensorClockFallingPolarity = SENSOR_CLOCK_POLARITY_LOW; /* not use */
    sensor_info->SensorHsyncPolarity = SENSOR_CLOCK_POLARITY_LOW; // inverse with datasheet
    sensor_info->SensorVsyncPolarity = SENSOR_CLOCK_POLARITY_LOW;
    sensor_info->SensorInterruptDelayLines = 4; /* not use */
    sensor_info->SensorResetActiveHigh = FALSE; /* not use */
    sensor_info->SensorResetDelayCount = 5; /* not use */

    sensor_info->SensroInterfaceType = imgsensor_info.sensor_interface_type;
    sensor_info->MIPIsensorType = imgsensor_info.mipi_sensor_type;
    sensor_info->SettleDelayMode = imgsensor_info.mipi_settle_delay_mode;
    sensor_info->SensorOutputDataFormat = imgsensor_info.sensor_output_dataformat;

    sensor_info->CaptureDelayFrame = imgsensor_info.cap_delay_frame;
    sensor_info->PreviewDelayFrame = imgsensor_info.pre_delay_frame;
    sensor_info->VideoDelayFrame = imgsensor_info.video_delay_frame;
    sensor_info->HighSpeedVideoDelayFrame = imgsensor_info.hs_video_delay_frame;
    sensor_info->SlimVideoDelayFrame = imgsensor_info.slim_video_delay_frame;

    sensor_info->SensorMasterClockSwitch = 0; /* not use */
    sensor_info->SensorDrivingCurrent = imgsensor_info.isp_driving_current;

    sensor_info->AEShutDelayFrame = imgsensor_info.ae_shut_delay_frame;          /* The frame of setting shutter default 0 for TG int */
    sensor_info->AESensorGainDelayFrame = imgsensor_info.ae_sensor_gain_delay_frame;    /* The frame of setting sensor gain */
    sensor_info->AEISPGainDelayFrame = imgsensor_info.ae_ispGain_delay_frame;
    sensor_info->IHDR_Support = imgsensor_info.ihdr_support;
    sensor_info->IHDR_LE_FirstLine = imgsensor_info.ihdr_le_firstline;
    sensor_info->SensorModeNum = imgsensor_info.sensor_mode_num;

    sensor_info->SensorMIPILaneNumber = imgsensor_info.mipi_lane_num;
    sensor_info->SensorClockFreq = imgsensor_info.mclk;
    sensor_info->SensorClockDividCount = 3; /* not use */
    sensor_info->SensorClockRisingCount = 0;
    sensor_info->SensorClockFallingCount = 2; /* not use */
    sensor_info->SensorPixelClockCount = 3; /* not use */
    sensor_info->SensorDataLatchCount = 2; /* not use */

    sensor_info->MIPIDataLowPwr2HighSpeedTermDelayCount = 0;
    sensor_info->MIPICLKLowPwr2HighSpeedTermDelayCount = 0;
    sensor_info->SensorWidthSampling = 0;  // 0 is default 1x
    sensor_info->SensorHightSampling = 0;    // 0 is default 1x
    sensor_info->SensorPacketECCOrder = 1;

    switch (scenario_id) {
        case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
            sensor_info->SensorGrabStartX = imgsensor_info.pre.startx;
            sensor_info->SensorGrabStartY = imgsensor_info.pre.starty;

            sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount = imgsensor_info.pre.mipi_data_lp2hs_settle_dc;

            break;
        case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
            sensor_info->SensorGrabStartX = imgsensor_info.cap.startx;
            sensor_info->SensorGrabStartY = imgsensor_info.cap.starty;

            sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount = imgsensor_info.cap.mipi_data_lp2hs_settle_dc;

            break;
        case MSDK_SCENARIO_ID_VIDEO_PREVIEW:

            sensor_info->SensorGrabStartX = imgsensor_info.normal_video.startx;
            sensor_info->SensorGrabStartY = imgsensor_info.normal_video.starty;

            sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount = imgsensor_info.normal_video.mipi_data_lp2hs_settle_dc;

            break;
        case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
            sensor_info->SensorGrabStartX = imgsensor_info.hs_video.startx;
            sensor_info->SensorGrabStartY = imgsensor_info.hs_video.starty;

            sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount = imgsensor_info.hs_video.mipi_data_lp2hs_settle_dc;

            break;
        case MSDK_SCENARIO_ID_SLIM_VIDEO:
            sensor_info->SensorGrabStartX = imgsensor_info.slim_video.startx;
            sensor_info->SensorGrabStartY = imgsensor_info.slim_video.starty;

            sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount = imgsensor_info.slim_video.mipi_data_lp2hs_settle_dc;

            break;
        default:
            sensor_info->SensorGrabStartX = imgsensor_info.pre.startx;
            sensor_info->SensorGrabStartY = imgsensor_info.pre.starty;

            sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount = imgsensor_info.pre.mipi_data_lp2hs_settle_dc;
            break;
    }

    return ERROR_NONE;
}    /*    get_info  */


static kal_uint32 control(MSDK_SCENARIO_ID_ENUM scenario_id, MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
                      MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
    LOG_INF("scenario_id = %d\n", scenario_id);
    spin_lock(&imgsensor_drv_lock);
    imgsensor.current_scenario_id = scenario_id;
    spin_unlock(&imgsensor_drv_lock);
    switch (scenario_id) {
        case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
            preview(image_window, sensor_config_data);
            break;
        case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
            capture(image_window, sensor_config_data);
            break;
        case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
            normal_video(image_window, sensor_config_data);
            break;
        case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
            hs_video(image_window, sensor_config_data);
            break;
        case MSDK_SCENARIO_ID_SLIM_VIDEO:
            slim_video(image_window, sensor_config_data);
            break;
        default:
            LOG_INF("Error ScenarioId setting");
            preview(image_window, sensor_config_data);
            return ERROR_INVALID_SCENARIO_ID;
    }
    return ERROR_NONE;
}    /* control() */



static kal_uint32 set_video_mode(UINT16 framerate)
{//This Function not used after ROME
    LOG_INF("framerate = %d\n ", framerate);
    // SetVideoMode Function should fix framerate
    if (framerate == 0)
        // Dynamic frame rate
        return ERROR_NONE;
    spin_lock(&imgsensor_drv_lock);
    if ((framerate == 300) && (imgsensor.autoflicker_en == KAL_TRUE))
        imgsensor.current_fps = 296;
    else if ((framerate == 150) && (imgsensor.autoflicker_en == KAL_TRUE))
        imgsensor.current_fps = 146;
    else
        imgsensor.current_fps = framerate;
    spin_unlock(&imgsensor_drv_lock);
    set_max_framerate(imgsensor.current_fps,1);

    return ERROR_NONE;
}

static kal_uint32 set_auto_flicker_mode(kal_bool enable, UINT16 framerate)
{
    LOG_INF("enable = %d, framerate = %d \n", enable, framerate);
    spin_lock(&imgsensor_drv_lock);
    if (enable) //enable auto flicker
        imgsensor.autoflicker_en = KAL_TRUE;
    else //Cancel Auto flick
        imgsensor.autoflicker_en = KAL_FALSE;
    spin_unlock(&imgsensor_drv_lock);
    return ERROR_NONE;
}


static kal_uint32 set_max_framerate_by_scenario(MSDK_SCENARIO_ID_ENUM scenario_id, MUINT32 framerate)
{
    kal_uint32 frame_length;

    LOG_INF("scenario_id = %d, framerate = %d\n", scenario_id, framerate);

    switch (scenario_id) {
        case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
            frame_length = imgsensor_info.pre.pclk / framerate * 10 / imgsensor_info.pre.linelength;
            spin_lock(&imgsensor_drv_lock);
            imgsensor.dummy_line = (frame_length > imgsensor_info.pre.framelength) ? (frame_length - imgsensor_info.pre.framelength) : 0;
            imgsensor.frame_length = imgsensor_info.pre.framelength + imgsensor.dummy_line;
            imgsensor.min_frame_length = imgsensor.frame_length;
            spin_unlock(&imgsensor_drv_lock);
            //set_dummy();
            break;
        case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
            if(framerate == 0)
                return ERROR_NONE;
            frame_length = imgsensor_info.normal_video.pclk / framerate * 10 / imgsensor_info.normal_video.linelength;
            spin_lock(&imgsensor_drv_lock);
            imgsensor.dummy_line = (frame_length > imgsensor_info.normal_video.framelength) ? (frame_length - imgsensor_info.normal_video.framelength) : 0;
            imgsensor.frame_length = imgsensor_info.normal_video.framelength + imgsensor.dummy_line;
            imgsensor.min_frame_length = imgsensor.frame_length;
            spin_unlock(&imgsensor_drv_lock);
            //set_dummy();
            break;
        case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
        	  if (imgsensor.current_fps == imgsensor_info.cap1.max_framerate) {
                frame_length = imgsensor_info.cap1.pclk / framerate * 10 / imgsensor_info.cap1.linelength;
                spin_lock(&imgsensor_drv_lock);
		            imgsensor.dummy_line = (frame_length > imgsensor_info.cap1.framelength) ? (frame_length - imgsensor_info.cap1.framelength) : 0;
		            imgsensor.frame_length = imgsensor_info.cap1.framelength + imgsensor.dummy_line;
		            imgsensor.min_frame_length = imgsensor.frame_length;
		            spin_unlock(&imgsensor_drv_lock);
            } else {
        		    if (imgsensor.current_fps != imgsensor_info.cap.max_framerate)
                    LOG_INF("Warning: current_fps %d fps is not support, so use cap's setting: %d fps!\n",framerate,imgsensor_info.cap.max_framerate/10);
                frame_length = imgsensor_info.cap.pclk / framerate * 10 / imgsensor_info.cap.linelength;
                spin_lock(&imgsensor_drv_lock);
		            imgsensor.dummy_line = (frame_length > imgsensor_info.cap.framelength) ? (frame_length - imgsensor_info.cap.framelength) : 0;
		            imgsensor.frame_length = imgsensor_info.cap.framelength + imgsensor.dummy_line;
		            imgsensor.min_frame_length = imgsensor.frame_length;
		            spin_unlock(&imgsensor_drv_lock);
            }
            //set_dummy();
            break;
        case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
            frame_length = imgsensor_info.hs_video.pclk / framerate * 10 / imgsensor_info.hs_video.linelength;
            spin_lock(&imgsensor_drv_lock);
            imgsensor.dummy_line = (frame_length > imgsensor_info.hs_video.framelength) ? (frame_length - imgsensor_info.hs_video.framelength) : 0;
            imgsensor.frame_length = imgsensor_info.hs_video.framelength + imgsensor.dummy_line;
            imgsensor.min_frame_length = imgsensor.frame_length;
            spin_unlock(&imgsensor_drv_lock);
            //set_dummy();
            break;
        case MSDK_SCENARIO_ID_SLIM_VIDEO:
            frame_length = imgsensor_info.slim_video.pclk / framerate * 10 / imgsensor_info.slim_video.linelength;
            spin_lock(&imgsensor_drv_lock);
            imgsensor.dummy_line = (frame_length > imgsensor_info.slim_video.framelength) ? (frame_length - imgsensor_info.slim_video.framelength): 0;
            imgsensor.frame_length = imgsensor_info.slim_video.framelength + imgsensor.dummy_line;
            imgsensor.min_frame_length = imgsensor.frame_length;
            spin_unlock(&imgsensor_drv_lock);
            //set_dummy();
            break;
        default:  //coding with  slim video2 scenario by default
            frame_length = imgsensor_info.slim_video.pclk / framerate * 10 / imgsensor_info.slim_video.linelength;
            spin_lock(&imgsensor_drv_lock);
            imgsensor.dummy_line = (frame_length > imgsensor_info.slim_video.framelength) ? (frame_length - imgsensor_info.slim_video.framelength): 0;
            imgsensor.frame_length = imgsensor_info.slim_video.framelength + imgsensor.dummy_line;
            imgsensor.min_frame_length = imgsensor.frame_length;
            spin_unlock(&imgsensor_drv_lock);
            //set_dummy();
            LOG_INF("error scenario_id = %d, we use slim video2 scenario \n", scenario_id);
            break;
    }
    return ERROR_NONE;
}


static kal_uint32 get_default_framerate_by_scenario(MSDK_SCENARIO_ID_ENUM scenario_id, MUINT32 *framerate)
{
    LOG_INF("scenario_id = %d\n", scenario_id);

    switch (scenario_id) {
        case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
            *framerate = imgsensor_info.pre.max_framerate;
            break;
        case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
            *framerate = imgsensor_info.normal_video.max_framerate;
            break;
        case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
            *framerate = imgsensor_info.cap.max_framerate;
            break;
        case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
            *framerate = imgsensor_info.hs_video.max_framerate;
            break;
        case MSDK_SCENARIO_ID_SLIM_VIDEO:
            *framerate = imgsensor_info.slim_video.max_framerate;
            break;
        default:
            break;
    }

    return ERROR_NONE;
}


static kal_uint32 feature_control(MSDK_SENSOR_FEATURE_ENUM feature_id,
                             UINT8 *feature_para,UINT32 *feature_para_len)
{
    UINT16 *feature_return_para_16=(UINT16 *) feature_para;
    UINT16 *feature_data_16=(UINT16 *) feature_para;
    UINT32 *feature_return_para_32=(UINT32 *) feature_para;
    UINT32 *feature_data_32=(UINT32 *) feature_para;
    unsigned long long *feature_data=(unsigned long long *) feature_para;
    unsigned long long *feature_return_para=(unsigned long long *) feature_para;

    SENSOR_WINSIZE_INFO_STRUCT *wininfo;
    MSDK_SENSOR_REG_INFO_STRUCT *sensor_reg_data=(MSDK_SENSOR_REG_INFO_STRUCT *) feature_para;

    LOG_INF("feature_id = %d\n", feature_id);
    switch (feature_id) {
        case SENSOR_FEATURE_GET_PERIOD:
            *feature_return_para_16++ = imgsensor.line_length;
            *feature_return_para_16 = imgsensor.frame_length;
            *feature_para_len=4;
            break;
        case SENSOR_FEATURE_GET_PIXEL_CLOCK_FREQ:
            *feature_return_para_32 = imgsensor.pclk;
            *feature_para_len=4;
            break;
        case SENSOR_FEATURE_SET_ESHUTTER:
            set_shutter(*feature_data);
            break;
        case SENSOR_FEATURE_SET_NIGHTMODE:
            night_mode((BOOL) *feature_data);
            break;
   	case SENSOR_FEATURE_SET_GAIN:
            set_gain((UINT16) *feature_data);
            break;
        case SENSOR_FEATURE_SET_FLASHLIGHT:
            break;
        case SENSOR_FEATURE_SET_ISP_MASTER_CLOCK_FREQ:
            break;
        case SENSOR_FEATURE_SET_REGISTER:
            write_cmos_sensor(sensor_reg_data->RegAddr, sensor_reg_data->RegData);
            break;
        case SENSOR_FEATURE_GET_REGISTER:
            sensor_reg_data->RegData = read_cmos_sensor(sensor_reg_data->RegAddr);
            break;
        case SENSOR_FEATURE_GET_LENS_DRIVER_ID:
            // get the lens driver ID from EEPROM or just return LENS_DRIVER_ID_DO_NOT_CARE
            // if EEPROM does not exist in camera module.
            *feature_return_para_32=LENS_DRIVER_ID_DO_NOT_CARE;
            *feature_para_len=4;
            break;
        case SENSOR_FEATURE_SET_VIDEO_MODE:
            set_video_mode(*feature_data);
            break;
        case SENSOR_FEATURE_CHECK_SENSOR_ID:
            get_imgsensor_id(feature_return_para_32);
            break;
        case SENSOR_FEATURE_SET_AUTO_FLICKER_MODE:
            set_auto_flicker_mode((BOOL)*feature_data_16,*(feature_data_16+1));
            break;
        case SENSOR_FEATURE_SET_MAX_FRAME_RATE_BY_SCENARIO:
            set_max_framerate_by_scenario((MSDK_SCENARIO_ID_ENUM)*feature_data, *(feature_data+1));
            break;
        case SENSOR_FEATURE_GET_DEFAULT_FRAME_RATE_BY_SCENARIO:
            get_default_framerate_by_scenario((MSDK_SCENARIO_ID_ENUM)*feature_data, (MUINT32 *)(uintptr_t)(*(feature_data+1)));
            break;
        case SENSOR_FEATURE_SET_TEST_PATTERN:
            set_test_pattern_mode((BOOL)*feature_data);
            break;
        case SENSOR_FEATURE_GET_TEST_PATTERN_CHECKSUM_VALUE: //for factory mode auto testing
            *feature_return_para_32 = imgsensor_info.checksum_value;
            *feature_para_len=4;
            break;
        case SENSOR_FEATURE_SET_FRAMERATE:
            LOG_INF("current fps :%d\n", *feature_data);
            spin_lock(&imgsensor_drv_lock);
            imgsensor.current_fps = *feature_data;
            spin_unlock(&imgsensor_drv_lock);
            break;
        case SENSOR_FEATURE_SET_HDR:
            LOG_INF("ihdr enable :%d\n", (BOOL)*feature_data);
            spin_lock(&imgsensor_drv_lock);
            imgsensor.ihdr_en = (BOOL)*feature_data;
            spin_unlock(&imgsensor_drv_lock);
            break;
        case SENSOR_FEATURE_GET_CROP_INFO:
            LOG_INF("SENSOR_FEATURE_GET_CROP_INFO scenarioId:%d\n", *feature_data);
            wininfo = (SENSOR_WINSIZE_INFO_STRUCT *)(uintptr_t)(*(feature_data+1));

            switch (*feature_data) {
                case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
                    memcpy((void *)wininfo,(void *)&imgsensor_winsize_info[1],sizeof(SENSOR_WINSIZE_INFO_STRUCT));
                    break;
                case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
                    memcpy((void *)wininfo,(void *)&imgsensor_winsize_info[2],sizeof(SENSOR_WINSIZE_INFO_STRUCT));
                    break;
                case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
                    memcpy((void *)wininfo,(void *)&imgsensor_winsize_info[3],sizeof(SENSOR_WINSIZE_INFO_STRUCT));
                    break;
                case MSDK_SCENARIO_ID_SLIM_VIDEO:
                    memcpy((void *)wininfo,(void *)&imgsensor_winsize_info[4],sizeof(SENSOR_WINSIZE_INFO_STRUCT));
                    break;
                case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
                default:
                    memcpy((void *)wininfo,(void *)&imgsensor_winsize_info[0],sizeof(SENSOR_WINSIZE_INFO_STRUCT));
                    break;
            }
            break;
        case SENSOR_FEATURE_SET_IHDR_SHUTTER_GAIN:
            LOG_INF("SENSOR_SET_SENSOR_IHDR LE=%d, SE=%d, Gain=%d\n",(UINT16)*feature_data,(UINT16)*(feature_data+1),(UINT16)*(feature_data+2));
            ihdr_write_shutter_gain((UINT16)*feature_data,(UINT16)*(feature_data+1),(UINT16)*(feature_data+2));
            break;
	default:
            break;
    }

    return ERROR_NONE;
}    /*    feature_control()  */

static SENSOR_FUNCTION_STRUCT sensor_func = {
    open,
    get_info,
    get_resolution,
    feature_control,
    control,
    close
};

UINT32 AR1335_QTECH_MIPI_RAW_SensorInit(PSENSOR_FUNCTION_STRUCT *pfFunc)
{
    /* To Do : Check Sensor status here */
    if (pfFunc!=NULL)
        *pfFunc=&sensor_func;
    return ERROR_NONE;
}    /*    AR1335_QTECH_MIPI_RAW_SensorInit    */
