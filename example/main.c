#include "../webscope.h"
#include <stdio.h>
#include <stdint.h>
#ifndef _WIN32
    #include <unistd.h>
#endif

static void sleep_millis(uint32_t millis)
{
    #ifdef _WIN32
        Sleep(millis);
    #else
        usleep(millis * 1000);
    #endif
}

int main(int argc, char **argv)
{
    webscope_open(1337);
    printf("Listening on localhost:1337...");

    while (1) {
        webscope_update();

        printf(".");
        fflush(stdout);
    //  printf("%f ", webscope_value("gravity", 1.0f, 0.0f, 2.0f));
    //
        sleep_millis(100);
    }

    webscope_close();
}
