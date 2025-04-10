#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define handle_error(msg) \
do { perror(msg); exit(EXIT_FAILURE); } while (0)

#define MAX_SIZE 1024 * 10
#define MAX_BODY_SIZE 1024 * 8
#define LISTEN_BACKLOG 50

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

void free_headers(http_header_t *headers) {
    http_header_t *ent = headers;
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

void print_http_resp(http_resp_t *resp) {
    printf("%s %d %s\r\n", resp->version, resp->status_code, resp->status_message);
    http_header_t *ent = resp->headers_list;
    for (; ent; ent = ent->next) {
        printf("%s: %s\r\n", ent->name, ent->value);
    }
    if (resp->body) {
        printf("\r\n%s", resp->body);
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

void read_file(char *buffer, char *path, size_t max_size) {
    FILE* fd = fopen(path, "rb");

    size_t file_size;
    fseek(fd, 0L, SEEK_END);
    if (max_size > (file_size = ftell(fd))) {
        max_size =  file_size;
    }
    rewind(fd);

    size_t read_size = fread(buffer, max_size, 1, fd);
    if (read_size != 1) {
        handle_error("fread error");
    } 
}

void prepare_resp_buf(char* resp_buf, http_req_t *req) {
    resp_buf[0] = '\0';

    http_resp_t resp;
    resp.headers_list = NULL;
    resp.body = malloc(MAX_BODY_SIZE);
    memset(resp.body, '\0', MAX_BODY_SIZE);
    resp.version = req->version;

    if (fopen(req->url + 1, "rb")) {
        read_file(resp.body, req->url + 1, MAX_BODY_SIZE);
        resp.status_message = "OK";
        resp.status_code = 200;
    } else {
        strcpy(resp.body, "File Not Found");
        resp.status_message = "Not Found";
        resp.status_code = 404;
    }

    char resp_line[128] = {0};
    sprintf(resp_line, "%s %d %s\r\n", resp.version,
            resp.status_code, resp.status_message);

    strcat(resp_buf, resp_line);

    char content_length[64];
    strcat(resp_buf, "Content-Type: text/html\r\n");
    sprintf(content_length, "Content-Length: %zu\r\n", strlen(resp.body));
    strcat(resp_buf, content_length);  

    strcat(resp_buf, "\r\n");

    strcat(resp_buf, resp.body);
    strcat(resp_buf, "\r\n");  
    
    free_headers(resp.headers_list);
    free(resp.body);
}

int main(int argc, char **argv) {
    if (argc != 2 || atoi(argv[1]) == 0) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(1);
    }

    const int PORT = atoi(argv[1]);

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

        char req_buf[MAX_SIZE] = {0};
        if (read(cfd, req_buf, MAX_SIZE) == -1) {
            close(cfd);
            continue;
        }

        http_req_t req;
        parse_http_req(req_buf, &req); 

        char resp_buf[MAX_SIZE] = {0};
        prepare_resp_buf(resp_buf, &req);

        write(cfd, resp_buf, strlen(resp_buf));

        free_headers(req.headers_list);
        close(cfd);
    }

    close(sfd);
    return 0;
}
