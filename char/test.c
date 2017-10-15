#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#define DEVICE_NAME "/dev/mychar"
#define SIZE 256
#define err_handler(en,msg) do{errno=en; perror(msg);exit(EXIT_SUCCESS);}while(0)
static char message[SIZE];
int main(){
	int ret,fd;
	char StringtoSend[SIZE];
	fd=open(DEVICE_NAME,O_RDWR);
	if(fd<0)
		err_handler(fd,"open");
	printf("Enter short string to send kernel module: \n");
	scanf("%[^\n]%*c",StringtoSend);
	printf("Writing msg to Device [%s]:\n",StringtoSend);
	ret=write(fd,StringtoSend,strlen(StringtoSend));
	if(ret<0)
	 	err_handler(ret,"write");
	printf("Press Enter to read from device..\n");
	getchar();
	printf("Reading from Device...\n");
	ret=read(fd,message,strlen(StringtoSend));
	if(ret<0)
		err_handler(ret,"read");
	printf("The received message is: [%s]\n",message);
	printf("End of the program\n");
	return 0;
}
