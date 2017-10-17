#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include "rtc.h"

#define err_handler(en,msg) do{errno=en; perror(msg);exit(EXIT_SUCCESS);}while(0)
#define DEVICE_NAME "/dev/myrtc"

int main(){
	struct rtc_time time;
	int fd,ret;
	fd=open(DEVICE_NAME,O_RDWR);
	if(fd<0)
		err_handler(fd,"open");
	time.sec=10;
	time.min=11;
	time.hour=12;
	time.day=3;
	time.mon=10;
	time.year=17;
	ret=write(fd, &time, sizeof(time));
	if(ret<0)
		err_handler(ret,"read");
	close(fd);
}

