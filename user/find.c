#include "../kernel/types.h"
#include "../user/user.h"
#include "../kernel/stat.h"
#include "../kernel/fs.h"
#define STD_ERR 2

char *fmtname(char *path)
{
    char *p;

    // Find first character after last slash.
    for (p = path + strlen(path); p >= path && *p != '/'; p--)
        ;
    p++;

    return p;
}

void find(char *path, char *filename)
{
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    if ((fd = open(path, 0)) < 0)
    {
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }

    if (fstat(fd, &st) < 0)
    {
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }
    switch (st.type)
    {
    case T_FILE:
        if (strcmp(fmtname(path), filename) == 0) //此时说明找到对应的文件
            printf("%s\n", path);
        break;

    case T_DIR:
        if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf)
        {
            printf("find: path too long\n");
            break;
        }
        strcpy(buf, path);
        p = buf + strlen(buf);
        *p++ = '/';
        while (read(fd, &de, sizeof(de)) == sizeof(de))
        {
            //跳过不符合要求的数据
            if (de.inum == 0 || strcmp(de.name, "..") == 0 || strcmp(de.name, ".") == 0)
                continue;
            memmove(p, de.name, DIRSIZ);
            p[DIRSIZ] = 0;
            
            find(buf, filename);
        }
        break;
    }
    close(fd);
}

int main(int argc, char **argv)
{
    if (argc != 3) //说明此时参数错误，应该报错
    {
        fprintf(STD_ERR, "usage: find directory filename\n");
        exit(1);
    }
    find(argv[1], argv[2]);
    exit(0);
}