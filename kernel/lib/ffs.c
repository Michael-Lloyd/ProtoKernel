#include <lib/ffs.h>

int ffs(int i)
{
    if (i == 0)
        return 0;

    int n = 1;
    if ((i & 0xffff) == 0) {
        n += 16;
        i >>= 16;
    }
    if ((i & 0xff) == 0) {
        n += 8;
        i >>= 8;
    }
    if ((i & 0xf) == 0) {
        n += 4;
        i >>= 4;
    }
    if ((i & 0x3) == 0) {
        n += 2;
        i >>= 2;
    }
    if ((i & 0x1) == 0) {
        n += 1;
    }
    return n;
}
