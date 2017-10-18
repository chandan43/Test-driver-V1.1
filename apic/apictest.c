#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>

#include "apic.h"
#define Device_path "/dev/myapic"
#define err_handler(en,msg) do{errno=en;perror(msg);exit(EXIT_SUCCESS);}while(0) 
#define SIZE 1
int main(){
	int fd,ret,ch,i;
	int ID[SIZE],IRQ[SIZE];
	fd=open(Device_path,O_RDWR);
	if(fd<0)
		err_handler(fd,"open");
	while(1){
		printf("Enter the choice\n");
		printf("-------------------\n");
		printf("1.APIC_GETID\n");
		printf("2.APIC_GETIRQ\n");
		printf("3.EXIT\n");
		printf("-------------------\n");
		scanf("%d",&ch);
		switch(ch){
			case 1:
				ret=ioctl(fd,APIC_GETID,ID);
				if(ret<0)
					err_handler(fd,"ioctl");
				printf("ID:\t");
				for(i=0;i<SIZE;i++){
					printf("%d",ID[i]);
				}
				printf("\n");
				break;
			case 2:
				ret=ioctl(fd,APIC_GETIRQ,IRQ);
				if(ret<0)
					err_handler(fd,"ioctl");
				printf("IRQ:\t");
				for(i=0;i<SIZE;i++){
					printf("%d",IRQ[i]);
				}
				printf("\n");
				break;
			case 3: 
				exit(1);
				
		}
	}
	return 0;
}

