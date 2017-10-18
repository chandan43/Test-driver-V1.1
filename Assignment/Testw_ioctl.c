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
int main(){
	int fd,ret;
	char StringtoSend[SIZE];
	fd= open(DEVICE_PATH,O_RDWR);
	if(fd<0)
		err_handelr(fd,"open");
	printf("Enter string to send kernel module: \n");
	scanf("%[^\n]%*c",StringtoSend);
	printf("Writing msg to Device [%s]:\n",StringtoSend);
	ret=ioctl(fd,FILE_WRITE,StringtoSend);
	if(ret<0)
	        err_handelr(fd,"ioctl");
	getchar();
	return 0;
}

