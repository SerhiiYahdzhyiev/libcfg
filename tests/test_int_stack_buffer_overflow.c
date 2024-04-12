#include <stdio.h>
#include "../include/cfg.h"

int main()
{
    int err;

    int value;

    err = cfg_load("./test_1.cfg");
    if( err )
    {
        return err;
    }

    err = cfg_get_setting("my_int", &value);
    if( err ) goto gracefull_exit;

    printf("Integer value: %d\n", value);

    return 0;

gracefull_exit:
    cfg_free();
    return err;
}
