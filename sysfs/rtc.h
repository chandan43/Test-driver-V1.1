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
	unsigned int sec;
	unsigned int min;
	unsigned int hour;
	unsigned int day;
	unsigned int mon;
	unsigned int year;
};
