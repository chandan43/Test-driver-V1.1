#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#define err_handelr(en,msg) do{errno=en; perror(msg);exit(EXIT_SUCCESS);}while(0)
#define DEVICE_NAME "/dev/char_dev1"
#define SIZE 1024

static char message[SIZE];
int main(){
	int fd,ret;
	fd= open(DEVICE_NAME,O_RDWR);
	if(fd<0)
		err_handelr(fd,"open");
	printf("Press Enter to read from device..\n");
	getchar();
	printf("Reading from Device...\n");
	ret=read(fd,message,SIZE);
	if(ret<0)
		err_handelr(ret,"read");
	printf("The received message is: [%s]\n",message);
	return 0;
}

