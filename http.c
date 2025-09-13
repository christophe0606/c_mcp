#define _GNU_SOURCE

#include "config.h"
#include "http.h"
#include "mcp.h"

// cc -O2 -pthread rt_http_control.c -o rt_http_control
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* ===========================
   Simple MPSC queue for JSON lines
   =========================== */
#ifndef QUEUE_CAP
#define QUEUE_CAP 1024
#endif

#ifndef MSG_MAX
#define MSG_MAX 4096
#endif

//#define DEBUG_TRACE


typedef struct {
    char data[MSG_MAX];
    int cfd;
} msg_t;

typedef struct {
    msg_t ring[QUEUE_CAP];
    size_t head; // next pop
    size_t tail; // next push
    pthread_mutex_t mu;
} msg_queue_t;

static void queue_init(msg_queue_t* q) {
    memset(q, 0, sizeof(*q));
    pthread_mutex_init(&q->mu, NULL);
}
static bool queue_try_push(msg_queue_t* q, const char* line,int cfd) {
#if defined(DEBUG_TRACE)
    printf("Enqueuing %s\n",line);
#endif
    pthread_mutex_lock(&q->mu);
    size_t next_tail = (q->tail + 1) % QUEUE_CAP;
    if (next_tail == q->head) { // full
        pthread_mutex_unlock(&q->mu);
        return false;
    }
    q->ring[q->tail].cfd = cfd;
    snprintf(q->ring[q->tail].data, MSG_MAX, "%s", line);
    q->tail = next_tail;
    pthread_mutex_unlock(&q->mu);
    return true;
}
static bool queue_try_pop(msg_queue_t* q, char out[MSG_MAX],int *cfd) {
    bool ok = false;
    pthread_mutex_lock(&q->mu);
    if (q->head != q->tail) {
        snprintf(out, MSG_MAX, "%s", q->ring[q->head].data);
        *cfd = q->ring[q->head].cfd;
        q->head = (q->head + 1) % QUEUE_CAP;
        ok = true;
    }
    pthread_mutex_unlock(&q->mu);
    return ok;
}

/* Expose non-blocking dequeue to your realtime loop */
static msg_queue_t g_cmd_queue;
static inline bool try_dequeue(char out[MSG_MAX],int *cfd) {
    return queue_try_pop(&g_cmd_queue, out,cfd);
}

/* ===========================
   Minimal HTTP parser (enough for POST /cmd)
   =========================== */

static int set_sockopts(int fd) {
    int yes = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) return -1;

    // modest recv timeout so  doesn't block the thread forever
    struct timeval tv = {.tv_sec = 2, .tv_usec = 0};
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) return -1;
    return 0;
}

static int create_listen_socket(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    if (set_sockopts(fd) < 0) { close(fd); return -1; }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { close(fd); return -1; }
    if (listen(fd, 64) < 0) { close(fd); return -1; }
    return fd;
}

static ssize_t recv_all(int fd, char* buf, size_t want) {
    size_t got = 0;
    while (got < want) {
        ssize_t n = recv(fd, buf + got, want - got, 0);
        if (n == 0) return (ssize_t)got; // peer closed
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        got += (size_t)n;
    }
    return (ssize_t)got;
}

static void http_400(int cfd, const char* msg) {
#if defined(DEBUG_TRACE)
    printf("HTTP 400: %s\n", msg);
#endif
    dprintf(cfd, "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Type: text/plain\r\nContent-Length: %zu\r\n\r\n%s",
            strlen(msg), msg);
}
static void http_404(int cfd) {
#if defined(DEBUG_TRACE)
    printf("HTTP 404\n");
#endif
    const char* m = "not found";
    dprintf(cfd, "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Type: text/plain\r\nContent-Length: %zu\r\n\r\n%s",
            strlen(m), m);
}

static void http_405(int cfd) {
#if defined(DEBUG_TRACE)
    printf("HTTP 405\n");
#endif
    const char* m = "not allowed";
    dprintf(cfd, "HTTP/1.1 405 Method Not Allowed\r\nConnection: close\r\nContent-Type: text/plain\r\nContent-Length: %zu\r\n\r\n%s",
            strlen(m), m);
}

void http_202(int cfd) {
#if defined(DEBUG_TRACE)
    printf("HTTP 202\n");
#endif
    const char* m = "Accepted";
    dprintf(cfd, "HTTP/1.1 202 Accepted\r\nConnection: close\r\nContent-Type: text/plain\r\nContent-Length: %zu\r\n\r\n%s",
            strlen(m), m);
}
void http_200_json(int cfd, const char* body) {
#if defined(DEBUG_TRACE)
    printf("HTTP 200: %s\n\n", body);
#endif
    size_t len = strlen(body);
    dprintf(cfd, "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n\r\n%s", len, body);
}

static void trim_trailing_newlines(char* s) {
    size_t n = strlen(s);
    while (n && (s[n-1] == '\n' || s[n-1] == '\r')) s[--n] = 0;
}

char hdr[8192*2];

/* Handle exactly: POST /cmd HTTP/1.1, with single-line JSON body */
static void handle_http_client(int cfd) {
    // Read until we have headers (\r\n\r\n)
    size_t used = 0;
    for (;;) {
        if (used >= sizeof(hdr)) { http_400(cfd, "headers too large"); return; }
        ssize_t n = recv(cfd, hdr + used, sizeof(hdr) - used, 0);
        if (n <= 0) { return; }
        used += (size_t)n;
        // search for end of headers
        char* p = NULL;
        for (size_t i = 3; i < used; ++i) {
            if (hdr[i-3]=='\r' && hdr[i-2]=='\n' && hdr[i-1]=='\r' && hdr[i]=='\n') {
                p = &hdr[i+1]; // first byte after CRLFCRLF
                break;
            }
        }
        if (!p) continue;

        // Parse request line (must start at hdr)
        char method[8], path[64], version[16];
        if (sscanf(hdr, "%7s %63s %15s", method, path, version) != 3) {
            http_400(cfd, "bad request line"); return;
        }

#if defined(DEBUG_TRACE)
        printf("HTTP %s %s\n", method, path);
#endif
        if (strcmp(method, "GET") == 0)
        {
           if (strcmp(path, "/health") == 0)
           {
               http_200_json(cfd, "{\"status\":\"ok\"}\n");
               return;
           }

           else if (strcmp(path, "/mcp") == 0)
           {
               http_400(cfd, "GET not supported on /mcp, use POST\n");
               return;
           }
           else 
           {
               http_404(cfd); return;
           }
        }

        // Only POST /cmd
        if (strcmp(method, "POST") != 0 || strcmp(path, "/mcp") != 0) {
            http_404(cfd); return;
        }

        // Find Content-Length
        size_t content_length = 0;
        {
            // crude header parse (case-insensitive not required if you control client)
            char* cl = strcasestr(hdr, "Content-Length:");
            if (!cl) { http_400(cfd, "missing content-length"); return; }
            if (sscanf(cl, "Content-Length: %zu", &content_length) != 1) 
            {
                if (sscanf(cl, "content-length: %zu", &content_length) != 1)
                {
                   http_400(cfd, "bad content-length"); return;
                }
            }
            if (content_length >= MSG_MAX) { http_400(cfd, "body too large"); return; }
        }

        // Compute already-buffered body bytes
        size_t header_bytes = (size_t)(p - hdr);
        size_t have_in_buf = (used > header_bytes) ? (used - header_bytes) : 0;

        char body[MSG_MAX];
        if (have_in_buf > 0) {
            size_t copy = have_in_buf > content_length ? content_length : have_in_buf;
            memcpy(body, p, copy);
            // If there are extra pipelined bytes we ignore them; we close anyway.
            // Read remaining if needed:
            if (copy < content_length) {
                ssize_t m = recv_all(cfd, body + copy, content_length - copy);
                if (m < 0 || (size_t)m != content_length - copy) { return; }
            }
        } else {
            ssize_t m = recv_all(cfd, body, content_length);
            if (m < 0 || (size_t)m != content_length) { return; }
        }

        body[content_length] = 0;
        trim_trailing_newlines(body);           // your convention: one-line JSON
        // Enqueue (non-blocking); if full, drop with 503-ish JSON
        if (!queue_try_push(&g_cmd_queue, body,cfd)) {
            http_400(cfd, "Command queue full\n");
        } 
        return;
    }
}

/* ===========================
   HTTP server thread
   =========================== */
typedef struct {
    int listen_fd;
    int send_fd;
    _Atomic bool running;
} http_server_t;

static void* http_thread_main(void* arg) {
    http_server_t* srv = (http_server_t*)arg;
    while (atomic_load(&srv->running)) {
        struct sockaddr_in cli;
        socklen_t slen = sizeof(cli);
        int cfd = accept(srv->listen_fd, (struct sockaddr*)&cli, &slen);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            // brief nap to avoid hot loop on transient errors
            struct timespec ts = {.tv_sec=0, .tv_nsec=50*1000*1000};
            nanosleep(&ts, NULL);
            continue;
        }
        handle_http_client(cfd);
    }
    return NULL;
}

static bool http_server_start(http_server_t* srv, uint16_t port, pthread_t* out_thr) {
    srv->listen_fd = create_listen_socket(port);
    if (srv->listen_fd < 0) return false;
    atomic_store(&srv->running, true);
    if (pthread_create(out_thr, NULL, http_thread_main, srv) != 0) {
        close(srv->listen_fd);
        return false;
    }
    return true;
}
static void http_server_stop(http_server_t* srv, pthread_t thr) {
    atomic_store(&srv->running, false);
    // Kick accept() by closing listener
    shutdown(srv->listen_fd, SHUT_RDWR);
    close(srv->listen_fd);
    pthread_join(thr, NULL);
}

static http_server_t srv;
static  pthread_t http_thr;
char line[MSG_MAX];


void process_http()
{
    int cfd;
    while (try_dequeue(line,&cfd)) {
        dispatch(line,cfd);
        close(cfd);
    }
}

int init_http()
{
    queue_init(&g_cmd_queue);

    
    if (!http_server_start(&srv, MCP_PORT, &http_thr)) {
        fprintf(stderr, "Failed to start HTTP server on port %u: %s\n", MCP_PORT, strerror(errno));
        return 1;
    }
    fprintf(stderr, "HTTP control listening on http://0.0.0.0:%u \n", MCP_PORT);
    return 0;
}

void end_http()
{
  http_server_stop(&srv, http_thr);
}