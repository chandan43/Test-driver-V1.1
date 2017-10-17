#include <stdio.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

int main(){
        int fd,i;
        unsigned char data;
        fd = open("/dev/myclock",O_RDONLY);
        if(fd < 0){ 
                perror("myclock:");
                exit(1);
        }
        data =0x04;
        for(i=0;i<6;i++){
                read(fd,&data,1);
                printf(" .... %x....\n",data);
//              perror("aa");
        }
	return 0;
}
