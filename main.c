#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define handle_error(msg) \
do { perror(msg); exit(EXIT_FAILURE); } while (0)

#define MAX_LEN 5120
#define LISTEN_BACKLOG 50
#define PORT 8080

typedef struct http_header_t {
    char *name;          
    char *value;         
    struct http_header_t *next; 
} http_header_t;

typedef struct {
    char *method;     
    char *url;         
    char *version;      
    http_header_t *headers_list; 
    char *body;          
} http_req_t;

typedef struct {
    char* version;        
    short status_code;         
    char* status_message; 
    http_header_t *headers_list; 
    char *body;
} http_resp_t;

void free_http_req(http_req_t *req) {
    http_header_t *ent = req->headers_list;
    for (;ent;) {
        http_header_t *next = ent->next;
        free(ent);

        ent = next;
    }
}

void print_http_req(http_req_t *req) {
    printf("%s %s %s\r\n", req->method, req->url, req->version);
    http_header_t *ent = req->headers_list;
    for (; ent; ent = ent->next) {
        printf("%s: %s\r\n", ent->name, ent->value);
    }
    if (req->body) {
        printf("\r\n%s", req->body);
    }
    printf("\r\n");
}

void parse_req_line(char* req_line, http_req_t *req) {
    char *saveptr;
    req->method = strtok_r(req_line, " ", &saveptr);
    req->url = strtok_r(NULL, " ", &saveptr);
    req->version = strtok_r(NULL, " ", &saveptr);
    req->version[strcspn(req->version, "\r")] = '\0';
}

void add_header(http_header_t **headers_list, char* name, char* value) {
    http_header_t *ent = malloc(sizeof(http_header_t));
    if (ent == NULL) {
        handle_error("malloc error");
    }
    ent->name = name;
    ent->value = value;
    ent->next = *headers_list;
    *headers_list = ent;
}

void parse_header_line(char* header_line, http_req_t *req) { 
    char *saveptr;
    char *name = strtok_r(header_line, " ", &saveptr);
    name[strcspn(name, ":")] = '\0';
    char *value = strtok_r(NULL, "\r", &saveptr);
    add_header(&req->headers_list, name, value);
}

void parse_http_req(char* req_buf, http_req_t *http_req) {
    char *saveptr, *header_line;
    char *req_line = strtok_r(req_buf, "\n", &saveptr);
    parse_req_line(req_line, http_req);
    http_req->headers_list = NULL;

    while (1) {
        header_line = strtok_r(NULL, "\n", &saveptr);
        if (strcmp(header_line, "\r") != 0)
            parse_header_line(header_line, http_req);
        else break;
    }
    http_req->body = strtok_r(NULL, "\r", &saveptr);
}

void prepare_resp(char* resp_buf, http_req_t *req) {
    resp_buf[0] = '\0';

    char body[] = "Hello World";
    size_t body_len = strlen(body);

    strcat(resp_buf, req->version);
    strcat(resp_buf, " 200 OK\r\n");

    char content_length[50];
    strcat(resp_buf, "Content-Type: text/plain\r\n");
    sprintf(content_length, "Content-Length: %zu\r\n", body_len);
    strcat(resp_buf, content_length);  

    strcat(resp_buf, "\r\n");

    strcat(resp_buf, body);
    strcat(resp_buf, "\r\n");  
}

int main() {
    int reuse = 1; 
    int result, sfd;
    sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR,
                   &reuse, sizeof(reuse)) < 0) {
        handle_error("setsockopt error");
        close(sfd);
    }

    struct sockaddr_in saddr, caddr;
    socklen_t client_addrlen = sizeof(caddr);

    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(PORT);
    inet_pton(AF_INET, "0.0.0.0", &saddr.sin_addr);

    result = bind(sfd, (struct sockaddr*)&saddr, sizeof(saddr));
    if (result == -1) {
        handle_error("bind error");
        close(sfd);
    }

    if (listen(sfd, LISTEN_BACKLOG) == -1) {
        handle_error("listen error");
        close(sfd);
    }

    while (1) {
        memset(&caddr, 0, sizeof(caddr));
        int cfd = accept(sfd, (struct sockaddr*)&caddr, &client_addrlen);
        if (cfd == -1) {
            continue;
        }

        char req_buf[MAX_LEN] = {0};
        if (read(cfd, req_buf, MAX_LEN) == -1) {
            close(cfd);
            continue;
        }

        http_req_t http_req;
        parse_http_req(req_buf, &http_req); 
        print_http_req(&http_req);

        char resp_buf[MAX_LEN] = {0};
        prepare_resp(resp_buf, &http_req);

        write(cfd, resp_buf, strlen(resp_buf));

        free_http_req(&http_req);
        close(cfd);
    }

    close(sfd);
    return 0;
}
