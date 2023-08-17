#include "../kernel/types.h"
#include "../kernel/param.h"
#include "../user/user.h"
#define STD_ERR 2

int main(int argc, char *argv[])
{
    char buf[512];
    char *full_argv[MAXARG];
    int i;
    int len;

    for (int i = 1; i < argc; i++)
    {
        full_argv[i - 1] = argv[i]; // 把参数赋值给full_argv
    }

    full_argv[argc] = 0; // 说明此时已到结尾

    while (1)
    {
        i = 0;
        while (1)
        {
            len = read(0, &buf[i], 1); // 从标准输入当中一字节一字节读取数据
            if (len == 0 || buf[i] == '\n')
                break;
            i++;
        }
        if (i == 0) // 说明此时没有数据
            break;

        buf[i] = 0; // 截断字符串
        full_argv[argc - 1] = buf;//将用户输入的命令保存到 full_argv 数组中

        if (fork() == 0) // 说明为子进程
        {
            //执行新加载的指令
            exec(full_argv[0], full_argv);
            exit(0);
        }
        else
        {
            wait(0);
        }
    }
    exit(0);
}