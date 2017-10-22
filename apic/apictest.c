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
	int ID[SIZE],IRQ[SIZE],irqstatus,irqtype;
	fd=open(Device_path,O_RDWR);
	if(fd<0)
		err_handler(fd,"open");
	while(1){
		printf("Enter the choice\n");
		printf("-------------------\n");
		printf("1.APIC_GETID\n");
		printf("2.APIC_GETIRQ\n");
		printf("3.APIC_GETIRQSTATUS\n");
		printf("4.APIC_GETIRQTYPE\n");
		printf("5.EXIT\n");
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
				printf("Enter the irq no[0 to 23] for IRQSTATUS\n");
				scanf("%d",&irqstatus);
				ret=ioctl(fd,APIC_GETIRQSTATUS,irqstatus);
				if(ret<0)
					err_handler(fd,"ioctl");
				if(ret)
					printf("Enabled\n");
				else
				 	printf("Disabled\n");
				break;
			case 4:
				printf("Enter the irq no[0 to 23] for IRQTYPE\n");
				scanf("%d",&irqtype);
				ret=ioctl(fd,APIC_GETIRQTYPE,irqtype);
				ret=ioctl(fd,APIC_GETIRQTYPE,i);
				if(ret<0)
					err_handler(fd,"ioctl");
				if(ret==0)
					printf("Signal type is : Fixed\n");
				else if(ret==1)
					printf("Signal type is : Lowest Priority\n");
				else if(ret==2)
					printf("Signal type is : SMI\n");
				else if(ret==3)
					printf("Signal type is : Reserved\n");
				else if(ret==4)
					printf("Signal type is : NMI\n");
				else if(ret==5)
					printf("Signal type is : INIT\n");
				else if(ret==6)
					printf("Signal type is : Reserved\n");
				else if(ret==7)
					printf("Signal type is : ExtINT\n");
				break;
			case 5: 
				exit(1);
				
		}
	}
	return 0;
}

