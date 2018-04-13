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
    printf("Press return to start listening on port 1337...\n");
    getchar();

    webscope_open(1337);

    for (unsigned int counter = 0; ; counter++)
    {
        webscope_update();

        float gravity = webscope_value("Gravity", 0.5f, 0.0f, 2.0f);

        if (gravity < 1.0f) {
            float featheriness = webscope_value("Featheriness", 0.0f, -100.0f, 100.0f);
            printf("%u\t Low gravity:  %f   Featheriness:  %f\n", counter, gravity, featheriness);
        } else {
            float crushingness = webscope_value("Crushingness", 0.0f, -100.0f, 100.0f);
            printf("%u\tHigh gravity:  %f   Crushingness:  %f\n", counter, gravity, crushingness);
        }

        sleep_millis(100);
    }

    webscope_close();
}