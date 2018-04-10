#include "webscope.h"
#include <stdio.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifdef _WIN32
    #define sleep Sleep
#endif

int main(int argc, char **argv)
{
    webscope_open(1337);

    while (1) {
        webscope_update();

        printf(".");
    //  printf("%f ", webscope_value("gravity", 1.0f, 0.0f, 2.0f));

        sleep(100);
    }

    webscope_close();
}