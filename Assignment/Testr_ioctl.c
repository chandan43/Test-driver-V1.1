#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include "charioctl_drv.h"

#define err_handelr(en,msg) do{errno=en; perror(msg);exit(EXIT_SUCCESS);}while(0)
#define DEVICE_PATH "/dev/char_dvr"
#define SIZE 1024
static char message[SIZE];
int main(){
	int fd,ret;
	fd= open(DEVICE_PATH,O_RDWR);
	if(fd<0)
		err_handelr(fd,"open");
	printf("Press Enter to read from device..\n");
	getchar();
	printf("Reading from Device...\n");
	ret=ioctl(fd,FILE_READ,message);
	if(ret<0)
	        err_handelr(fd,"ioctl");
	printf("The received message is: [%s]\n",message);
	return 0;
}

