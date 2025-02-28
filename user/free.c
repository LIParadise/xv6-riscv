#include "kernel/types.h"
#include "user/user.h"

int main(const int argc, const char *argv[])
{
    if (1 == argc)
    {
        uint64 pages = get_free_pages();
        printf("Free mem in pages: %lu, i.e. %lu KB == %lu MB\n", pages, pages * 4, pages / 256);
        exit(0);
    }
    else
    {
        exit(1);
    }
}
