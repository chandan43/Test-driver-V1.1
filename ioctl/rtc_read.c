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
	ret=read(fd, &time, sizeof(struct rtc_time));
	if(ret<0)
		err_handler(ret,"read");
	printf("sec : %x\n", time.sec);
	printf("min : %x\n", time.min);
	printf("hour: %x\n", time.hour);
	printf("day : %x\n", time.day);
	printf("mon : %x\n", time.mon);
	printf("year: %x\n", time.year);
	close(fd);
}

