#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ctype.h>
#include <regex.h>
#include <signal.h>
#include <pthread.h>


inline int httpd_listen(int s)
{
    int client;
    socklen_t ss;
    struct sockaddr_in their_addr;

    if((client = accept(s, (struct sockaddr *)&their_addr, &ss)) < 1) {
        perror("accept");
        return -1;
    }
    return client;
}

inline int httpd_recv(int sock_fd)
{
    ssize_t s = 0, total = 0;
    unsigned char buf[2048];

    /* recv da header [up to 1024*10 bytes] */
    do {
        s = recv(sock_fd, buf, 2048, 0);
        if(s < 1)
            break;

        total += s;
        if(total > 4) {
            if(buf[total-3] == '\n' &&
                buf[total-2] == '\r' &&
                buf[total-1] == '\n') {
                break;
            }
        }
    } while(total < 1024*10);
    return total;
}

inline ssize_t sock_send_all(int sock_fd, const void *buf, size_t len)
{
    size_t sent = 0, s;
    do {
        s = send(sock_fd, buf, len, 0);
        if(s < 1)
            break;
        sent += s;
    } while(sent < len);
    return sent;
}

struct httpd_data {
    int sock_fd;
    int stage;
} httpd_datas[64];

void *httpd_worker(void *noargs)
{
    int i = 0, misses = 0;
    struct httpd_data *h;
    char b1[] = "<br><div style='border:100000px solid black;padding:100000px;margin:100000px'>a<br>b<br></div><h1>C</h1><br>";
    char b2[] = "<p style='background:grey;font-size:100000px;padding:1000000px;margin:100000px'>1<br>2<br></p>";
    char header[] = "<html><head><title>garf</title></head><body>";
    char OK_200[] = "HTTP/1.1 200 OK\r\nContent-type: text/html; charset=UTF-8\r\nServer: garf v1.0\r\n\r\n";
    /* char footer; -- no footer */
    ssize_t sent = 0;
    pthread_detach(pthread_self());

    /* this loop is a little aggressive right now
     * because i'm lazy (no blocking or signal */
    while(1) {
        for(i = 0; i < 64; i++) {
            h = &httpd_datas[i];
            /*
            if(h->stage != 0) {
                fprintf(stderr, "debug: client @ index %d on stage %d\n", i, h->stage);
            }*/
            switch(h->stage) {
                case 0: break;
                case 1:
                    misses = 0;
                    sent = sock_send_all(h->sock_fd, OK_200, sizeof(OK_200));
                    if(sent < 1)
                        goto destroy_client;
                    h->stage = 2;
                    break;
                case 2:
                    misses = 0;
                    sent = sock_send_all(h->sock_fd, header, sizeof(header));
                    if(sent < 1)
                        goto destroy_client;
                    h->stage = 3;
                    break;
                case 3:
                    misses = 0;
                    sent = sock_send_all(h->sock_fd, b1, sizeof(b1));
                    if(sent < 1)
                        goto destroy_client;
                    h->stage = 4;
                    break;
                case 4:
                    misses = 0;
                    sent = sock_send_all(h->sock_fd, b2, sizeof(b2));
                    if(sent < 1)
                        goto destroy_client;
                    h->stage = 3;
                    break;
                default:
                    fprintf(stderr, "Unknown stage %d at index %d\n", h->stage, i);

            }
        }
        misses++;
        if(misses > 100000)
            misses = 100;
        usleep(100+misses);
        continue;
    destroy_client:
        fprintf(stderr, "Closing on stage %d at index %d\n", h->stage, i);
        close(h->sock_fd);
        h->stage = 0;
    }
}

void httpd_send(int sock_fd)
{
    char b1[] = "<br><div style='border:1px solid black;padding:1200px;margin:2000px'>a<br>b<br></div><h1>C</h1><br>";
    char b2[] = "<p style='background:grey;font-size:120px;padding:1px;margin:1px'>1<br>2<br></p>";
    char header[] = "<html><head><title>garf</title></head><body>";
    char OK_200[] = "HTTP/1.1 200 OK\r\nContent-type: text/html; charset=UTF-8\r\nServer: garf v1.0\r\n\r\n";
    /* char footer; -- no footer */
    ssize_t sent = 0;

    if(sock_send_all(sock_fd, OK_200, sizeof(OK_200)) > 0) {
        sent = sock_send_all(sock_fd, header, sizeof(header));
        while(sent > 0) {
            sent = sock_send_all(sock_fd, b2, sizeof(b2));
            if(sent > 0)
                sent = sock_send_all(sock_fd, b1, sizeof(b1));
        }
    } else {
        fprintf(stderr, "Failed to sent HTTP header\n");
    }
    close(sock_fd);
}


int httpd_init(int port)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    struct sockaddr_in sa;
    pthread_t th;

    memset(httpd_datas, '\0', sizeof(httpd_datas));

    if(s < 0)
        goto err;

    if(setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) < 0)
        goto err;

    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = INADDR_ANY;
    memset(sa.sin_zero, '\0', sizeof sa.sin_zero);
    if(bind(s, (struct sockaddr *)&sa, sizeof(struct sockaddr)) < 0)
        goto err;

    if(listen(s, 1000) < 0)
        goto err;

    if(pthread_create(&th, NULL, httpd_worker, (void *)NULL) != 0)
        goto err;

    return s;
err:
    perror("httpd_init");
    close(s);
    return -1;
}

int httpd_assign_client(int client_fd)
{
    int i = 0;
    struct httpd_data *h;

    for(i = 0; i < 64; i++) {
        h = &httpd_datas[i];
        if(h->stage == 0) {
            h->sock_fd = client_fd;
            h->stage = 1;
            return 1;
        }
    }
    return -1;
}

void usage(void)
{
    printf("garf <port>\n");
    fflush(stdout);
    exit(1);
}

int main(int argc, char **argv)
{
    int s, c;

    if(argc != 2)
        usage();

    s = httpd_init(atoi(argv[1]));
    if(s < 1)
        exit(1);
    signal(SIGPIPE, SIG_IGN);

    while(1) {
        c = httpd_listen(s);
        if(httpd_assign_client(c) < 1) {
            fprintf(stderr, "Failed to assign client socket\n");
            close(c);
        }
        /* 1 client version
         * httpd_send(c);
         */
    }

    return 0;
}
