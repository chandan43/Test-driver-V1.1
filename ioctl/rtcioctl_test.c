#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/ioctl.h>

#include "rtc.h"

#define err_handler(en,msg) do{errno=en; perror(msg);exit(EXIT_SUCCESS);}while(0)
#define DEVICE_NAME "/dev/myrtc"

int main(){
	struct rtc_time time;
	int fd,ret;
	fd=open(DEVICE_NAME,O_RDWR);
	if(fd<0)
		err_handler(fd,"open");
	ioctl(fd, SET_SECOND, 11);
	ioctl(fd, SET_MINUTE, 11);
	ioctl(fd, SET_HOUR, 11);
	ioctl(fd, SET_DAY, 11);
	ioctl(fd, SET_MONTH, 11);
	ioctl(fd, SET_YEAR, 11);
	close(fd);
	return 0;
}

