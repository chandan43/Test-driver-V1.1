#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>

#include "charioctl.h"
#define Device_path "/dev/char_dev"
#define err_handler(en,msg) do{errno=en;perror(msg);exit(EXIT_SUCCESS);}while(0) 

int main(){
	int fd,ret;
	unsigned int data;
	printf("AA= %d\n",&data);
	//uint data=2048;
	fd=open(Device_path,O_RDWR);
	if(fd<0)
	   err_handler(fd,"open");
//	ret=ioctl(fd,FILLCHAR,'b');
//	ret=ioctl(fd,FILLZERO,0);
//	ret=ioctl(fd,FILLCHAR,'c');
	ret=ioctl(fd,GETSIZE,data);
	//ret=ioctl(fd,SETSIZE,data);
	//ret=ioctl(fd,FILLCHAR,'b');
	if(ret<0)
	   err_handler(fd,"ioctl");
	printf("Size of KBUFFER is %u\n",data);
	return 0;
}

