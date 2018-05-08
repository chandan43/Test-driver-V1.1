#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/epoll.h>

#define err_handelr(en,msg) do{errno=en; perror(msg);exit(EXIT_FAILURE);}while(0)
#define DEVICE_NAME "/dev/char_dev"
#define SIZE 1024
#define MAX_EVENTS 1

static char message[SIZE];
int main(){
	int fd;
	struct epoll_event ev, events[MAX_EVENTS];
	int opt, ret, device = 0;
	int epoll_fd, nfds = 0;

	fd = open(DEVICE_NAME,O_RDWR);
	if(fd<0)
		err_handelr(device,"open");
	epoll_fd = epoll_create(1);
	if (epoll_fd == -1) {
		fprintf(stderr, "epoll_create failed\n");
		close(fd);
		exit(EXIT_FAILURE);
	}
	ev.events = EPOLLPRI;
        ev.data.fd = fd;
        nfds = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);
        if (nfds == -1) {
                fprintf(stderr, "epoll_ctl failed");
		close(fd);
                exit(EXIT_FAILURE);
        }

	while (1) {
                int i;

                nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
                if (nfds == -1) {
                        fprintf(stderr, "epoll_wait failed\n");
                        break;
                }
                for (i = 0; i < nfds; i++) {
                        if (events[i].events & EPOLLPRI)
                                //nitrox_monitor(ev.data.fd);
				printf("Event Read\n");
                }
        }	
	close(fd);
	return 0;
}

