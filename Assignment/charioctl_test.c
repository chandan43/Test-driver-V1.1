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
	int fd,ret,ch;
	uint data;
	//uint data=2048;
	fd=open(Device_path,O_RDWR);
	if(fd<0)
		err_handler(fd,"open");
	while(1){
		printf("Enter the choice\n");
		printf("-------------------\n");
		printf("1.FILLZERO\n");
		printf("2.FILLCHAR\n");
		printf("3.GETSIZE\n");
		printf("4.SETSIZE\n");
		printf("5.EXIT\n");
		printf("-------------------\n");
		scanf("%d",&ch);
		switch(ch){
			case 1:
				ret=ioctl(fd,FILLZERO,0);
				if(ret<0)
					err_handler(fd,"ioctl");
				break;
			case 2:
				ret=ioctl(fd,FILLCHAR,'b');
				if(ret<0)
					err_handler(fd,"ioctl");
				break;
			case 3:
				ret=ioctl(fd,GETSIZE,data);
				if(ret<0)
					err_handler(fd,"ioctl");
				printf("Size of KBUFFER is %u\n",ret);
				break;
			case 4:
				printf("Enter the size of BUFFE Ex: 512,1024,2048 etc..\n");
				scanf("%d",&data);
				ret=ioctl(fd,SETSIZE,data);
				if(ret<0)
					err_handler(fd,"ioctl");
				break;
			case 5: 
				exit(1);
				
		}
	}
	return 0;
}

