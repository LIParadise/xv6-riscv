#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    printf("argv is %p\n", argv);
    for (int i = 0; i < argc; ++i)
    {
        printf("argv[%d] is %p\n", i, argv[i]);
    }
    exit(0);
}
