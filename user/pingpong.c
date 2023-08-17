#include"kernel/types.h"
#include"kernel/stat.h"
#include"user/user.h"

int main(int argc,int*argv[])
{
    int fd1[2];//0 is read,1 is write
    int fd2[2];
    char buf;

    pipe(fd1);
    pipe(fd2);

    //child
    if(!fork())
    {
        int pid=getpid();

        //close write of parent and read of child
        close(fd1[1]);
        close(fd2[0]);

        //child receive "ping" and print
        read(fd1[0],&buf,sizeof(buf));
        printf("%d: received ping\n", pid);

        //write to parent
        write(fd2[1],"a",sizeof(buf));

        exit(0);
    }

    //parent
    else 
    {
        int pid=getpid();

        //close read of parent and write of child
        close(fd1[0]);
        close(fd2[1]);

        //write to child
        write(fd1[1],"b",sizeof(buf));

        //parent receive "pong" and print
        read(fd2[0],&buf,sizeof(buf));
        printf("%d: received pong\n", pid);

        //等待进程子退出
        wait(0);
	    exit(0);
    }

    exit(0);
}