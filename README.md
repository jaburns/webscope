## webscope
Small library to enable runtime editing of values in a realtime C or C++ program from a browser.

![Demo](https://github.com/jaburns/webscope/raw/master/example/demo.gif)

### Usage
Simply include `webscope.c` and `webscope.h` in your project. There are no additional dependencies. On Windows the winsock2 lib file is pulled in to the linker through a `#pragma`, and on Linux and OSX the required functions are linked automatically. With webscope now available, place `webscope_open(PORT);` and `webscope_close();` around your main loop, and place `webscope_update();` inside of it somewhere. Now anywhere in your main loop you can call `webscope_value(name, default, min, max);` to get a web-editable float. You don't need to declare these ahead of time anywhere, webscope will bind them as they are discovered during runtime.

### Running the examples

##### OSX / Linux
```
gcc example/main.c webscope.c && ./a.out
```
##### Visual Studio on Windows
- Open and run the `example/webscope.sln` solution.
