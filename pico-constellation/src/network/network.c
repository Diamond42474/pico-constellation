#include "network/network.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#include "network/dhcpserver.h"
#include "network/dnsserver.h"
#include "c-logger.h"

#include "ui.h"

#define BSSID "pico-constellation"
#define PASSWORD "peregrine"
#define TCP_PORT 80
#define POLL_TIME_S 5
#define HTTP_GET "GET"
#define HTTP_RESPONSE_HEADERS "HTTP/1.1 %d OK\nContent-Length: %d\nContent-Type: text/html; charset=utf-8\nConnection: close\n\n"

typedef struct TCP_SERVER_T_
{
    struct tcp_pcb *server_pcb;
    bool complete;
    ip_addr_t gw;
} TCP_SERVER_T;

typedef struct TCP_CONNECT_STATE_T_
{
    struct tcp_pcb *pcb;
    int sent_len;
    char headers[512];
    char result[512];
    int header_len;
    int result_len;
    ip_addr_t *gw;
} TCP_CONNECT_STATE_T;

static err_t tcp_close_client_connection(TCP_CONNECT_STATE_T *con_state, struct tcp_pcb *client_pcb, err_t close_err);
static void tcp_server_close(TCP_SERVER_T *state);
static err_t tcp_server_sent(void *arg, struct tcp_pcb *pcb, u16_t len);
err_t tcp_server_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err);
static err_t tcp_server_poll(void *arg, struct tcp_pcb *pcb);
static void tcp_server_err(void *arg, err_t err);
static err_t tcp_server_accept(void *arg, struct tcp_pcb *client_pcb, err_t err);
static bool tcp_server_open(void *arg, const char *ap_name);
static bool parse_http_request(char *request_line, http_request_t *req);

static TCP_SERVER_T *state;
static dhcp_server_t dhcp_server;
static dns_server_t dns_server;
static http_contents_t http_contents;

int network_init(void)
{
    state = calloc(1, sizeof(TCP_SERVER_T));
    if (!state)
    {
        LOG_ERROR("failed to allocate state");
        return 1;
    }

    if (cyw43_arch_init())
    {
        LOG_ERROR("failed to initialise");
        return 1;
    }

    cyw43_arch_enable_ap_mode(BSSID, PASSWORD, CYW43_AUTH_WPA2_AES_PSK);

#define IP(x) (x)

    ip4_addr_t mask;
    IP(state->gw).addr = PP_HTONL(CYW43_DEFAULT_IP_AP_ADDRESS);
    IP(mask).addr = PP_HTONL(CYW43_DEFAULT_IP_MASK);

#undef IP

    // Start the dhcp server

    dhcp_server_init(&dhcp_server, &state->gw, &mask);

    // Start the dns server
    dns_server_init(&dns_server, &state->gw);

    if (!tcp_server_open(state, BSSID))
    {
        LOG_ERROR("failed to open server");
        return 1;
    }

    state->complete = false;
    return 0;
}

int network_deinit(void)
{
    tcp_server_close(state);
    dns_server_deinit(&dns_server);
    dhcp_server_deinit(&dhcp_server);
    cyw43_arch_deinit();
    return 0;
}

int network_task(void)
{
    return 0;
}

//

static err_t tcp_close_client_connection(TCP_CONNECT_STATE_T *con_state, struct tcp_pcb *client_pcb, err_t close_err)
{
    if (client_pcb)
    {
        assert(con_state && con_state->pcb == client_pcb);
        tcp_arg(client_pcb, NULL);
        tcp_poll(client_pcb, NULL, 0);
        tcp_sent(client_pcb, NULL);
        tcp_recv(client_pcb, NULL);
        tcp_err(client_pcb, NULL);
        err_t err = tcp_close(client_pcb);
        if (err != ERR_OK)
        {
            LOG_ERROR("close failed %d, calling abort", err);
            tcp_abort(client_pcb);
            close_err = ERR_ABRT;
        }
        if (con_state)
        {
            free(con_state);
        }
    }
    return close_err;
}

static void tcp_server_close(TCP_SERVER_T *state)
{
    if (state->server_pcb)
    {
        tcp_arg(state->server_pcb, NULL);
        tcp_close(state->server_pcb);
        state->server_pcb = NULL;
    }
}

static err_t tcp_server_sent(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    TCP_CONNECT_STATE_T *con_state = (TCP_CONNECT_STATE_T *)arg;
    LOG_DEBUG("tcp_server_sent %u", len);
    con_state->sent_len += len;
    if (con_state->sent_len >= con_state->header_len + con_state->result_len)
    {
        LOG_DEBUG("all done");
        return tcp_close_client_connection(con_state, pcb, ERR_OK);
    }
    return ERR_OK;
}

static http_method_t parse_http_method(const char *s)
{
    if (strcmp(s, "GET") == 0)
        return HTTP_METHOD_GET;
    if (strcmp(s, "POST") == 0)
        return HTTP_METHOD_POST;
    if (strcmp(s, "PUT") == 0)
        return HTTP_METHOD_PUT;
    if (strcmp(s, "DELETE") == 0)
        return HTTP_METHOD_DELETE;
    return HTTP_METHOD_UNKNOWN;
}

static bool parse_http_request(char *request_line, http_request_t *req)
{
    if (!request_line || !req)
        return false;

    char *method = request_line;
    char *space = strchr(method, ' ');
    if (!space)
        return false;

    *space++ = '\0';
    while (*space == ' ')
        space++;

    char *path = space;
    if (!*path)
        return false;

    char *end = strchr(path, ' ');
    if (!end)
        return false;

    *end = '\0';

    char *query = strchr(path, '?');
    if (query)
    {
        *query++ = '\0';
    }

    req->method = parse_http_method(method);
    if (req->method == HTTP_METHOD_UNKNOWN)
        return false;

    size_t path_len = strlen(path);
    if (path_len >= HTML_MAX_PATH_LEN)
        return false;
    memcpy(req->path, path, path_len + 1);

    if (query)
    {
        size_t query_len = strlen(query);
        if (query_len >= HTML_MAX_QUERY_LEN)
            return false;
        memcpy(req->query, query, query_len + 1);
    }
    else
    {
        req->query[0] = '\0';
    }

    return true;
}

err_t tcp_server_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    TCP_CONNECT_STATE_T *con_state = (TCP_CONNECT_STATE_T *)arg;
    if (!p)
    {
        LOG_INFO("connection closed");
        return tcp_close_client_connection(con_state, pcb, ERR_OK);
    }
    assert(con_state && con_state->pcb == pcb);

    // Print full request:

    if (p->tot_len > 0)
    {
        LOG_DEBUG("tcp_server_recv %d err %d", p->tot_len, err);

        // Copy the request into the buffer
        size_t copy_len = p->tot_len > sizeof(con_state->headers) - 1 ? sizeof(con_state->headers) - 1 : p->tot_len;
        pbuf_copy_partial(p, con_state->headers, copy_len, 0);
        con_state->headers[copy_len] = '\0';

        LOG_INFO("HTTP REQUEST: %s", con_state->headers);

        // Handle request
        http_request_t http_request;
        if (!parse_http_request(con_state->headers, &http_request))
        {
            LOG_ERROR("Failed to parse HTTP request");
            return tcp_close_client_connection(con_state, pcb, ERR_CLSD);
        }

        if (ui_handle_event(&http_contents, &http_request))
        {
            LOG_ERROR("Failed to handle UI event");
            return tcp_close_client_connection(con_state, pcb, ERR_CLSD);
        }

        con_state->result_len = http_contents.length;

        // Check we had enough buffer space
        // if (con_state->result_len > sizeof(con_state->result) - 1)
        // {
        //     LOG_ERROR("Too much result data %d", con_state->result_len);
        //     return tcp_close_client_connection(con_state, pcb, ERR_CLSD);
        // }

        // Generate web page
        if (con_state->result_len > 0)
        {
            con_state->header_len = snprintf(con_state->headers, sizeof(con_state->headers), HTTP_RESPONSE_HEADERS,
                                             200, con_state->result_len);
            if (con_state->header_len > sizeof(con_state->headers) - 1)
            {
                LOG_ERROR("Too much header data %d", con_state->header_len);
                return tcp_close_client_connection(con_state, pcb, ERR_CLSD);
            }
        }
        // else
        // {
        //     // Send redirect
        //     con_state->header_len = snprintf(con_state->headers, sizeof(con_state->headers), HTTP_RESPONSE_REDIRECT,
        //                                      ipaddr_ntoa(con_state->gw));
        //     LOG_INFO("Sending redirect %s", con_state->headers);
        // }

        // Send the headers to the client
        con_state->sent_len = 0;
        err_t err = tcp_write(pcb, con_state->headers, con_state->header_len, 0);
        if (err != ERR_OK)
        {
            LOG_ERROR("failed to write header data %d", err);
            return tcp_close_client_connection(con_state, pcb, err);
        }

        // Send the body to the client
        if (http_contents.length)
        {
            err = tcp_write(pcb, http_contents.contents, http_contents.length, 0);
            if (err != ERR_OK)
            {
                LOG_ERROR("failed to write result data %d", err);
                return tcp_close_client_connection(con_state, pcb, err);
            }
        }
        tcp_recved(pcb, p->tot_len);
    }
    pbuf_free(p);
    return ERR_OK;
}

static err_t tcp_server_poll(void *arg, struct tcp_pcb *pcb)
{
    TCP_CONNECT_STATE_T *con_state = (TCP_CONNECT_STATE_T *)arg;
    LOG_INFO("tcp_server_poll_fn");
    return tcp_close_client_connection(con_state, pcb, ERR_OK); // Just disconnect clent?
}

static void tcp_server_err(void *arg, err_t err)
{
    TCP_CONNECT_STATE_T *con_state = (TCP_CONNECT_STATE_T *)arg;
    if (err != ERR_ABRT)
    {
        LOG_ERROR("tcp_client_err_fn %d", err);
        tcp_close_client_connection(con_state, con_state->pcb, err);
    }
}

static err_t tcp_server_accept(void *arg, struct tcp_pcb *client_pcb, err_t err)
{
    TCP_SERVER_T *state = (TCP_SERVER_T *)arg;
    if (err != ERR_OK || client_pcb == NULL)
    {
        LOG_ERROR("failure in accept");
        return ERR_VAL;
    }
    LOG_DEBUG("client connected");

    // Create the state for the connection
    TCP_CONNECT_STATE_T *con_state = calloc(1, sizeof(TCP_CONNECT_STATE_T));
    if (!con_state)
    {
        LOG_ERROR("failed to allocate connect state");
        return ERR_MEM;
    }
    con_state->pcb = client_pcb; // for checking
    con_state->gw = &state->gw;

    // setup connection to client
    tcp_arg(client_pcb, con_state);
    tcp_sent(client_pcb, tcp_server_sent);
    tcp_recv(client_pcb, tcp_server_recv);
    tcp_poll(client_pcb, tcp_server_poll, POLL_TIME_S);
    tcp_err(client_pcb, tcp_server_err);

    return ERR_OK;
}

static bool tcp_server_open(void *arg, const char *ap_name)
{
    TCP_SERVER_T *state = (TCP_SERVER_T *)arg;
    LOG_INFO("starting server on port %d", TCP_PORT);

    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb)
    {
        LOG_ERROR("failed to create pcb");
        return false;
    }

    err_t err = tcp_bind(pcb, IP_ANY_TYPE, TCP_PORT);
    if (err)
    {
        LOG_ERROR("failed to bind to port %d", TCP_PORT);
        return false;
    }

    state->server_pcb = tcp_listen_with_backlog(pcb, 1);
    if (!state->server_pcb)
    {
        LOG_ERROR("failed to listen");
        if (pcb)
        {
            tcp_close(pcb);
        }
        return false;
    }

    tcp_arg(state->server_pcb, state);
    tcp_accept(state->server_pcb, tcp_server_accept);

    return true;
}