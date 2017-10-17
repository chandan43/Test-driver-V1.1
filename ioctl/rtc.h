#define  SECOND         0x00  
#define  MINUTE         0x02 
#define  HOUR           0x04
#define  DAY_IN_WEEK    0x06
#define  DAY            0x07
#define  MONTH          0x08
#define  YEAR           0x09

#define ADDRESS_REG     0x70
#define DATA_REG 	0x71

struct rtc_time {
	unsigned char sec;
	unsigned char min;
	unsigned char hour;
	unsigned char day;
	unsigned char mon;
	unsigned char year;
};

#define MY_MAGIC 'V'

#define SET_SECOND _IOW(MY_MAGIC,1,char) 
#define SET_MINUTE _IOW(MY_MAGIC,2,char)
#define SET_HOUR   _IOW(MY_MAGIC,3,char)
#define SET_DAY    _IOW(MY_MAGIC,4,char)
#define SET_MONTH  _IOW(MY_MAGIC,5,char)
#define SET_YEAR   _IOW(MY_MAGIC,6,char)	
