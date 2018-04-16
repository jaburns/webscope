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

#define MAX_HTTP_MESSAGE  65536
#define MAX_VALUE_LABEL     256

static const char PAGE_HTML[] = "<!DOCTYPE html><html><head><style>html, body {background-color: #292929;color: #ababab;font-family: monospace;padding-top: 10px;}table {margin: auto;}td {padding: 1px 5px 1px 5px;}.label {padding-left: 10px;padding-right: 5px;text-align: right;}.show {width: 100%;}.slider {width: 200px;}.text {background-color: #292929;border: 0;font-family: monospace;color: #ababab;width: 60px;padding-left: 3px;}.item {background-color: #383838}.item:not(.active) {background-color: #222;}button {background-color: #484848;color: #ababab;font-family: monospace;border: 0;}textarea {width: 100%;resize: none;margin-top: 3px;margin-right: 5px;background-color: #222;border: 0;color: #ababab;height: 64px;}</style></head><body><template id=\"itemTemplate\"><tr class=\"item\"><td class=\"label\"></td><td><input class=\"slider\" type=\"range\" /></td><td><input class=\"text\" type=\"text\" /></td><td><button class=\"reset\">Reset</button></td></tr></template><table id=\"list\"><tr><td colspan=\"4\"><button class=\"show\">Print All Values</button><textarea></textarea></td></tr></table><script>const valueList = [];const updateLoop = () =>fetch(window.location.href, { method: 'post', body: getPostBody() }).then(response => response.text()).then(parseResponseBody);const updateInterval = setInterval(updateLoop, 100);document.querySelector('.show').onclick = () =>document.querySelector('textarea').value = getPostBody();const getPostBody = () =>valueList.map(v => `${v.label}=${v.value};`).join('');const parseResponseBody = body => {const valueConfigSet = body.split(';');valueConfigSet.pop();valueList.forEach(x => x.active = false);valueConfigSet.forEach(receiveValueConfig);valueList.forEach(value =>value.element.classList[value.active ? 'add' : 'remove']('active'));};const receiveValueConfig = valueConfig => {const labelAndValues = valueConfig.split('=');const label = labelAndValues[0];const match = valueList.filter(x => x.label === label)[0];if (match) {match.active = true;} else {pushNewValue(label, labelAndValues[1].split(':').map(parseFloat));}};const pushNewValue = (label, config) => {const newValue = {label: label,value: config[0],defaultValue: config[0],min: config[1],max: config[2],active: true};initElements(newValue);valueList.push(newValue);};const initElements = value => {const newNode = document.querySelector('#itemTemplate').content.cloneNode(true);newNode.querySelector('.label').innerText = value.label;const slider = newNode.querySelector('.slider');slider.min = value.min;slider.max = value.max;slider.step = (value.max - value.min) / 1000;slider.value = value.defaultValue;const text = newNode.querySelector('.text');text.value = value.defaultValue;const reset = newNode.querySelector('.reset');slider.oninput = () => value.value = text.value = slider.value;reset.onclick = () => value.value = text.value = slider.value = value.defaultValue;text.onchange = () => {const x = parseFloat(text.value);if (isFinite(x)) value.value = slider.value = x;};value.element = newNode.querySelector('.item');const list = document.querySelector('#list');list.insertBefore(newNode, list.childNodes[list.childNodes.length - 1]);};</script></body></html>";

struct WebscopeValue
{
    const char *label;
    float value, min, max;
    bool active;
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

static bool is_post_request(const char *request)
{
    return strncmp(request, "POST", 4) == 0;
}

static const char *allocate_http_response(const char *body)
{
    static const int MAX_EXTRA_CONTENT = 16;
    static const char HEADERS[] = "HTTP 1.1/200 OK\nContent-Length: ";

    int body_len = strnlen(body, MAX_HTTP_MESSAGE);
    int buffer_size = sizeof(HEADERS) + MAX_EXTRA_CONTENT + body_len;

    char *response = malloc(buffer_size);
    snprintf(response, buffer_size, "%s%u\n\n%s", HEADERS, body_len, body);

    return response;
}

static void parse_posted_value(struct WebscopeState *state, const char *value_set, int value_set_len)
{
    static char buffer[MAX_VALUE_LABEL + 1];

    const char *equals_loc = strchr(value_set, '=');
    int label_len = equals_loc - value_set;

    if (label_len >= value_set_len || label_len > MAX_VALUE_LABEL) return;

    memcpy(buffer, value_set, label_len);
    buffer[label_len] = 0;

    struct WebscopeValue *value = find_value(state, buffer);

    if (value == NULL) return;

    int value_len = value_set_len - label_len - 1;
    memcpy(buffer, equals_loc + 1, value_len);
    buffer[value_len] = 0;

    value->value = strtof(buffer, NULL);
}

static void handle_post(struct WebscopeState *state, const char *request)
{
    const char *body = strrchr(request, '\n');
    if (body == NULL) return;

    body++;

    while (true) {
        const char *next_sep = strchr(body, ';');
        if (next_sep == NULL) break;

        parse_posted_value(state, body, next_sep - body);

        body = next_sep + 1;
    }
}

static char *handle_post_and_allocate_response(struct WebscopeState *state, const char *request)
{
    static const int MAX_FLOAT_SERIALIZED_SIZE = 16;

    handle_post(state, request);

    int buffer_len = state->value_count * (MAX_VALUE_LABEL + 3 * MAX_FLOAT_SERIALIZED_SIZE);
    char *buffer = malloc(buffer_len);
    buffer[0] = 0;

    int buffer_pos = 0;
    for (int i = 0; i < state->value_count; ++i) {
        if (! state->values[i].active) continue;

        buffer_pos += snprintf(
            buffer + buffer_pos, buffer_len - buffer_pos, "%s=%f:%f:%f;",
            state->values[i].label, state->values[i].value, state->values[i].min, state->values[i].max
        );

        state->values[i].active = false;
    }
    
    const char *response = allocate_http_response(buffer);

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
    static char buffer[MAX_HTTP_MESSAGE];
    
    if (g_state == NULL) {
        printf("ERROR: Called webscope_update while session was closed.");
        exit(1);
    }

    struct sockaddr_in remaddr;
    socklen_t slen = sizeof(remaddr);

    while (true)
    {
        socket_handle client = accept(g_state->socket_handle, (struct sockaddr*)&remaddr, &slen);
        if (client == INVALID_SOCKET) break;

        int msg_len = recv(client, buffer, sizeof(buffer) - 1, 0);
        if (msg_len < 0) break;
        
        buffer[msg_len] = 0;

        const char *response = is_post_request(buffer)
            ? handle_post_and_allocate_response(g_state, buffer)
            : allocate_http_response(PAGE_HTML);

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
        value = push_value(g_state, (struct WebscopeValue){ label, default_value, min, max, true });
    }

    value->active = true;

    return value->value;
}
