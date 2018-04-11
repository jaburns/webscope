#include "webscope.h"

#include <stdio.h>
#include <stdlib.h>
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
    #define INVALID_SOCKET (-1)
    typedef int socket_handle;
#endif

static const char PAGE_HTML[] = "<!DOCTYPE html><html><body><div><p id=\"response\">{}</p><button id=\"post\">Send POST</button></div><script>document.getElementById('post').onclick = () => {fetch(window.location.href, {method: 'post',body: 'Posting from JS'}).then(response => response.text()).then(body => document.getElementById('response').innerText = body);};</script></body></html>";

static const int MAX_HTTP_MESSAGE = 65536;
static const int MAX_VALUE_LABEL = 256;

struct WebscopeValue
{
    const char *label;
    float value, min, max;
};

struct WebscopeState
{
    int value_count;
    struct WebscopeValue *values;
    socket_handle socket_handle;
};

static struct WebscopeState *g_state = NULL;

static struct WebscopeValue *find_value(const struct WebscopeState *state, const char *label)
{
    for (int i = 0; i < state->value_count; ++i) {
        if (strcmp(label, state->values[i].label) == 0) {
            return &state->values[i];
        }
    }

    return NULL;
}

static struct WebscopeValue *push_value(struct WebscopeState *state, struct WebscopeValue value)
{
    struct WebscopeValue *old_values = state->values;
    struct WebscopeValue *new_values = malloc(sizeof(struct WebscopeValue) * (state->value_count + 1));

    if (old_values != NULL) {
        memcpy(new_values, old_values, sizeof(struct WebscopeValue) * state->value_count);
        free(old_values);
    }

    new_values[state->value_count] = value;

    state->values = new_values;
    state->value_count++;

    return &new_values[state->value_count - 1];
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
    #else
        close(socket);
    #endif
}

static bool is_get_request(const char *request)
{
    char compare[4] = "\0\0\0\0";
    memcpy(compare, request, 3);
    return strcmp(compare, "GET") == 0;
}

static char *allocate_http_response(const char *body)
{
    static const char HEADERS[] = "HTTP 1.1/200 OK\nContent-Length: ";

    int body_len = strnlen(body, MAX_HTTP_MESSAGE);
    int buffer_size = sizeof(HEADERS) + 16 + body_len;

    char *response = malloc(buffer_size);
    snprintf(response, buffer_size, "%s%u\n\n%s", HEADERS, body_len, body);

    return response;
}

static char *handle_post_and_allocate_response(struct WebscopeState *state, const char *request)
{
    int buffer_len = state->value_count * (MAX_VALUE_LABEL + 3 * 16);
    char *buffer = malloc(buffer_len);

    int buffer_pos = 0;
    for (int i = 0; i < state->value_count; ++i) {
        buffer_pos = snprintf(
            buffer + buffer_pos, buffer_len - buffer_pos, "%s=%f:%f:%f;",
            state->values[i].label, state->values[i].value, state->values[i].min, state->values[i].max
        );
    }
    
    char *response = allocate_http_response(buffer);

    free(buffer);

    return response;
}

void webscope_open(uint16_t port)
{
    if (g_state != NULL) {
        printf("ERROR: Called webscope_open while session was already open.");
        exit(1);
    }
    
    g_state = malloc(sizeof(struct WebscopeState));
    g_state->value_count = 0;
    g_state->values = NULL;
    g_state->socket_handle = open_socket(port);
}

void webscope_close()
{
    if (g_state == NULL) {
        printf("ERROR: Called webscope_close while session was already closed.");
        exit(1);
    }

    close_socket(g_state->socket_handle);

    if (g_state->value_count > 0) {
        free(g_state->values);
    }
    free(g_state);

    #ifdef _WIN32
        WSACleanup();
    #endif
}

void webscope_update()
{
    if (g_state == NULL) {
        printf("ERROR: Called webscope_update while session was closed.");
        exit(1);
    }

    struct sockaddr_in remaddr;
    socklen_t slen = sizeof(remaddr);
    char buffer[1024];

    while (true)
    {
        socket_handle client = accept(g_state->socket_handle, (struct sockaddr*)&remaddr, &slen);

        if (client == INVALID_SOCKET) break;

        memset(buffer, 0, sizeof(buffer));
        recv(client, buffer, sizeof(buffer) - 1, 0);

        char *response = is_get_request(buffer)
            ? allocate_http_response(PAGE_HTML)
            : handle_post_and_allocate_response(g_state, buffer);

        int response_len = strnlen(response, MAX_HTTP_MESSAGE);
        send(client, response, response_len, 0);
        free(response);

        close_socket(client);
    }
}

float webscope_value(const char *label, float default_value, float min, float max)
{
    if (g_state == NULL) {
        printf("ERROR: Called webscope_value while session was closed.");
        exit(1);
    }

    struct WebscopeValue *value = find_value(g_state, label);

    if (value == 0) {
        value = push_value(g_state, (struct WebscopeValue){ label, default_value, min, max });
    }

    return value->value;
}