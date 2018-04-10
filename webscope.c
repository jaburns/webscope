#include "webscope.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#ifdef _WIN32
    #pragma comment(lib, "ws2_32")
    #include <winsock2.h>
    typedef int socklen_t;
    typedef SOCKET socket_handle;
#else
    #include <unistd.h>
    #include <netdb.h>
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <fcntl.h>
    typedef int socket_handle;
#endif

static const char PAGE_HTML[] = "<!DOCTYPE html><html><body><div><p id=\"response\">{}</p><button id=\"post\">Send POST</button></div><script>document.getElementById('post').onclick = () => {fetch(window.location.href, {method: 'post',body: 'Posting from JS'}).then(response => response.text()).then(body => document.getElementById('response').innerText = body);};</script></body></html>";

static const int MAX_STRING = 65536;

struct WebscopeValue
{
    const char *label;
    float value, min, max;
};

static int s_value_count = 0;
static struct WebscopeValue *s_values = NULL;
static socket_handle s_socket_handle = INVALID_SOCKET;

static struct WebscopeValue *find_value(const char *label)
{
    for (int i = 0; i < s_value_count; ++i) {
        if (strcmp(label, s_values[i].label) == 0) {
            return &s_values[i];
        }
    }

    return NULL;
}

static struct WebscopeValue *push_value(struct WebscopeValue value)
{
    struct WebscopeValue *old_values = s_values;
    struct WebscopeValue *new_values = malloc(sizeof(struct WebscopeValue) * (s_value_count + 1));

    if (old_values != NULL) {
        memcpy(new_values, old_values, sizeof(struct WebscopeValue) * s_value_count);
        free(old_values);
    }

    new_values[s_value_count] = value;

    s_values = new_values;
    s_value_count++;

    return &new_values[s_value_count - 1];
}

static socket_handle open_socket(uint16_t port)
{
    socket_handle result;

    #ifdef _WIN32
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
            printf("ERROR: Failed to init winsock, error code: %i", WSAGetLastError());
            exit(1);
        }
    #endif

    result = socket(AF_INET, SOCK_STREAM, 0);
    if (result == INVALID_SOCKET) {
        printf("ERROR: Failed to create TCP socket.");
        exit(1);
    }

    #ifdef _WIN32
        u_long sock_mode_nonblocking = 1;
        ioctlsocket(result, FIONBIO, &sock_mode_nonblocking);
    #else
        fcntl(result, F_SETFL, O_NONBLOCK);
    #endif

    struct sockaddr_in myaddr;
    memset((char*)&myaddr, 0, sizeof(myaddr));
    myaddr.sin_family = AF_INET;
    myaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    myaddr.sin_port = htons(port);

    if (bind(result, (struct sockaddr*)&myaddr, sizeof(myaddr)) < 0) {
        printf("ERROR: Failed to bind socket to port %u", port);
        exit(1);
    }

    listen(result, 128);

    return result;
}

static void close_socket(socket_handle socket)
{
    #ifdef _WIN32
        closesocket(socket);
        WSACleanup();
    #else
        close(socket);
    #endif
}

static bool is_get_request(const char *request)
{
    char buf[4] = "\0\0\0\0";
    memcpy(buf, request, 3);
    return strcmp(buf, "GET") == 0;
}

static char *allocate_http_response(const char *body)
{
    static const char HEADERS[] = "HTTP 1.1/200 OK\nContent-Length: ";

    int body_len = strnlen(body, MAX_STRING);
    int buffer_size = sizeof(HEADERS) + 16 + body_len;

    char *response = malloc(buffer_size);
    sprintf_s(response, buffer_size, "%s%u\n\n%s", HEADERS, body_len, body);

    return response;
}

static char *handle_post_and_allocate_response(const char *request)
{
    printf(request);
    return allocate_http_response("Hello from POST handler.");
}

void webscope_open(uint16_t port)
{
    if (s_socket_handle != INVALID_SOCKET) {
        printf("ERROR: Called webscope_open while session was already open.");
        exit(1);
    }

    s_socket_handle = open_socket(port);
}

void webscope_close()
{
    if (s_socket_handle == INVALID_SOCKET) {
        printf("ERROR: Called webscope_close while session was already closed.");
        exit(1);
    }

    close_socket(s_socket_handle);
    s_socket_handle = INVALID_SOCKET;

    if (s_value_count > 0) {
        free(s_values);
        s_values = NULL;
        s_value_count = 0;
    }
}

void webscope_update()
{
    if (s_socket_handle == INVALID_SOCKET) {
        printf("ERROR: Called webscope_update while session was closed.");
        exit(1);
    }

    struct sockaddr_in remaddr;
    socklen_t slen = sizeof(remaddr);
    char buffer[1024];

    while (true)
    {
        socket_handle client = accept(s_socket_handle, (struct sockaddr*)&remaddr, &slen);
        if (client == INVALID_SOCKET) break;

        memset(buffer, 0, sizeof(buffer));
        recv(client, buffer, sizeof(buffer) - 1, 0);

        char *response = is_get_request(buffer)
            ? allocate_http_response(PAGE_HTML)
            : handle_post_and_allocate_response(buffer);

        int response_len = strnlen(response, MAX_STRING);
        send(client, response, response_len, 0);
        free(response);

        #ifdef _WIN32
            closesocket(client);
        #else
            close(client);
        #endif
    }
}

float webscope_value(const char *label, float default_value, float min, float max)
{
    struct WebscopeValue *value = find_value(label);

    if (value == 0) {
        value = push_value((struct WebscopeValue){ label, default_value, min, max });
    }

    return value->value;
}
