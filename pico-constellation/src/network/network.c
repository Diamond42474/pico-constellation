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
#define HTTP_RESPONSE_HEADERS                    \
    "HTTP/1.1 %d OK\r\n"                         \
    "Content-Length: %d\r\n"                     \
    "Content-Type: text/html; charset=utf-8\r\n" \
    "Connection: close\r\n"                      \
    "\r\n"

typedef struct TCP_SERVER_T_
{
    struct tcp_pcb *server_pcb;
    bool complete;
    ip_addr_t gw;
} TCP_SERVER_T;

typedef struct TCP_CONNECT_STATE_T_
{
    struct tcp_pcb *pcb;
    ip_addr_t *gw;

    char request[2048];
    size_t request_len;

    char response_headers[256];
    size_t response_header_len;

    size_t response_body_len;
    size_t sent_len;
} TCP_CONNECT_STATE_T;

static err_t tcp_close_client_connection(TCP_CONNECT_STATE_T *con_state, struct tcp_pcb *client_pcb, err_t close_err);
static void tcp_server_close(TCP_SERVER_T *state);
static err_t tcp_server_sent(void *arg, struct tcp_pcb *pcb, u16_t len);
err_t tcp_server_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err);
static err_t tcp_server_poll(void *arg, struct tcp_pcb *pcb);
static void tcp_server_err(void *arg, err_t err);
static err_t tcp_server_accept(void *arg, struct tcp_pcb *client_pcb, err_t err);
static bool tcp_server_open(void *arg, const char *ap_name);
static int parse_http_request(char *request_line, http_request_t *req);

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
    if (con_state->sent_len >= con_state->response_header_len + con_state->response_body_len)
    {
        LOG_DEBUG("all done");
        con_state->request_len = 0;
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

static int parse_http_request(char *request, http_request_t *req)
{
    if (!request || !req)
    {
        LOG_ERROR("request or req is NULL");
        return -1;
    }

    memset(req, 0, sizeof(*req));

    // Find the start of the body.
    char *body = strstr(request, "\r\n\r\n");
    if (body)
    {
        *body = '\0';
        body += 4;

        size_t body_len = strlen(body);
        if (body_len >= HTML_MAX_QUERY_LEN)
        {
            LOG_ERROR("Body too large");
            return -1;
        }

        memcpy(req->body, body, body_len + 1);
    }

    // Parse request line.
    char *method = request;
    char *space = strchr(method, ' ');
    if (!space)
    {
        LOG_ERROR("Malformed request");
        return -1;
    }

    *space++ = '\0';
    while (*space == ' ')
        space++;

    char *path = space;

    space = strchr(path, ' ');
    if (!space)
    {
        LOG_ERROR("Malformed request");
        return -1;
    }

    *space = '\0';

    // Split query string from path.
    char *query = strchr(path, '?');
    if (query)
    {
        *query++ = '\0';
    }

    req->method = parse_http_method(method);
    if (req->method == HTTP_METHOD_UNKNOWN)
    {
        LOG_ERROR("Unknown HTTP method");
        return -1;
    }

    if (strlen(path) >= HTML_MAX_PATH_LEN)
    {
        LOG_ERROR("Path too long");
        return -1;
    }

    strcpy(req->path, path);

    if (query)
    {
        if (strlen(query) >= HTML_MAX_QUERY_LEN)
        {
            LOG_ERROR("Query too long");
            return -1;
        }

        strcpy(req->query, query);
    }

    return 0;
}

static bool http_request_complete(TCP_CONNECT_STATE_T *con)
{
    char *end = strstr(con->request, "\r\n\r\n");
    if (!end)
        return false;

    size_t header_len = end - con->request + 4;

    char *cl = strstr(con->request, "Content-Length:");
    if (!cl)
        return true; // GET, HEAD, etc.

    size_t body_len = atoi(cl + 15);

    return con->request_len >= header_len + body_len;
}

err_t tcp_server_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    TCP_CONNECT_STATE_T *con = (TCP_CONNECT_STATE_T *)arg;

    if (p == NULL)
    {
        LOG_INFO("connection closed");
        return tcp_close_client_connection(con, pcb, ERR_OK);
    }

    if (err != ERR_OK)
    {
        pbuf_free(p);
        return tcp_close_client_connection(con, pcb, err);
    }

    // Ensure the request fits in our buffer.
    if (con->request_len + p->tot_len >= sizeof(con->request))
    {
        LOG_ERROR("HTTP request too large");
        pbuf_free(p);
        return tcp_close_client_connection(con, pcb, ERR_BUF);
    }

    // Append the received data.
    pbuf_copy_partial(
        p,
        con->request + con->request_len,
        p->tot_len,
        0);

    con->request_len += p->tot_len;
    con->request[con->request_len] = '\0';

    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);

    // Wait until the full HTTP request has arrived.
    if (!http_request_complete(con))
    {
        return ERR_OK;
    }

    // LOG_INFO("HTTP REQUEST:\n%s", con->request);

    http_request_t http_request;
    if (parse_http_request(con->request, &http_request))
    {
        LOG_ERROR("Failed to parse HTTP request");
        return tcp_close_client_connection(con, pcb, ERR_CLSD);
    }

    if (ui_handle_event(&http_contents, &http_request))
    {
        LOG_ERROR("Failed to handle UI event");
        return tcp_close_client_connection(con, pcb, ERR_CLSD);
    }

    con->response_body_len = http_contents.length;

    con->response_header_len = snprintf(
        con->response_headers,
        sizeof(con->response_headers),
        HTTP_RESPONSE_HEADERS,
        200,
        con->response_body_len);

    if (con->response_header_len >= sizeof(con->response_headers))
    {
        LOG_ERROR("Response headers too large");
        return tcp_close_client_connection(con, pcb, ERR_BUF);
    }

    con->sent_len = 0;

    LOG_INFO("pcb=%p", pcb);
    LOG_INFO("sndbuf=%u", tcp_sndbuf(pcb));
    LOG_INFO("sndqueuelen=%u", pcb->snd_queuelen);
    LOG_INFO("header_len=%u", con->response_header_len);
    LOG_INFO("body_len=%u", con->response_body_len);
    LOG_INFO("Response:\n%s", con->response_headers);

    err = tcp_write(
        pcb,
        con->response_headers,
        con->response_header_len,
        TCP_WRITE_FLAG_COPY);

    if (err != ERR_OK)
    {
        LOG_INFO("header=%u body=%u sndbuf=%u",
                 con->response_header_len,
                 con->response_body_len,
                 tcp_sndbuf(pcb));
        LOG_ERROR("Failed to send response headers: %d", err);
        return tcp_close_client_connection(con, pcb, err);
    }

    if (con->response_body_len)
    {
        err = tcp_write(
            pcb,
            http_contents.contents,
            con->response_body_len,
            TCP_WRITE_FLAG_COPY);

        if (err != ERR_OK)
        {
            LOG_INFO("header=%u body=%u sndbuf=%u",
                     con->response_header_len,
                     con->response_body_len,
                     tcp_sndbuf(pcb));
            LOG_ERROR("Failed to send response body: %d", err);
            return tcp_close_client_connection(con, pcb, err);
        }
    }

    tcp_output(pcb);

    // Ready for another request if the connection stays open.
    con->request_len = 0;

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