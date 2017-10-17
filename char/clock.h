

#define RTC_ADDRESS_BASE 0x70
#define RTC_DATA_BASE 0x71

#define SECOUND_CMD 0
#define MINUTES_CMD 2
#define HOURS_CMD 4
#define DAY_CMD 7
#define MONTH_CMD 8
#define YEAR_CMD 9

/* cmd is SECONDS_CMD .... YEARS_CMD... */
#define READ_FROM_CLOCK(cmd,data) outb_p(cmd,RTC_ADDRESS_BASE);data=inb_p(RTC_DATA_BASE);

/* cmd is SECONDS_CMD .... YEARS_CMD... and value is the value to be set */
#define WRITE_TO_CLOCK(cmd,value) outb_p(cmd,RTC_ADDRESS_BASE);outb_p(value,RTC_DATA_BASE);

