#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#define err_handelr(en,msg) do{errno=en; perror(msg);exit(EXIT_SUCCESS);}while(0)
#define DEVICE_NAME "/dev/char_dev"
#define SIZE 1024
int main(){
	int fd,ret;
	char StringtoSend[SIZE];
	fd= open(DEVICE_NAME,O_RDWR);
	if(fd<0)
		err_handelr(fd,"open");
	printf("Enter string to send kernel module: \n");
	scanf("%[^\n]%*c",StringtoSend);
	printf("Writing msg to Device [%s]:\n",StringtoSend);
	ret=write(fd, StringtoSend,strlen(StringtoSend));
	if(ret<0)
	        err_handelr(fd,"write");
	getchar();
	return 0;
}

