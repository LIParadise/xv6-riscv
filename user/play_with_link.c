#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

int main()
{
    // create file
    int fd = open("artifacts", O_RDWR | O_CREATE | O_TRUNC);
    link("artifacts", "artifacts_");
    close(fd);
    if (fork() == 0)
    {
        // child writes by different name
        int fd = open("artifacts_", O_RDWR);
        fprintf(fd, "written by child!\n");
        close(fd);
    }
    else
    {
        int fd = open("artifacts", O_RDWR);
        fprintf(fd, "written by parent!\n");
        close(fd);
    }
}
