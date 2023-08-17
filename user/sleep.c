#include "../kernel/types.h"  //kernel当中的类型定义头文件
#include "../user/user.h"     //user当中实现的有关函数的头文件

int main(int argc,char*argv[])
{
    int ticks=0;
    if(argc!=2)
    {
        fprintf(2,"usage: sleep sleep_number \n");//2代表标准错误输出
		exit(1);
    }
    ticks=atoi(argv[1]);
    sleep(ticks);
    exit(0);
}