#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
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
    char body[MAX_BODY_SIZE];          
} http_req_t;

typedef struct {
    char* version;        
    short status_code;         
    char* status_message; 
    http_header_t *headers_list; 
    char body[MAX_BODY_SIZE];          
} http_resp_t;

int is_reg_file(const char *path) {
    struct stat statbuf;
    if (stat(path, &statbuf) != 0) return 0;
    return S_ISREG(statbuf.st_mode);
}

int is_dir(const char *path) {
    struct stat statbuf;
    if (stat(path, &statbuf) != 0) return 0;
    return S_ISDIR(statbuf.st_mode);
}

void free_dir_content(char** dir_content) {
    int j;
    for (j = 0; dir_content[j]; j++) {
        free(dir_content[j]); 
    }
    free(dir_content[j]); 
    free(dir_content); 
}

char** get_dir_content(char *path) {
    int ent_count = 0;
    struct dirent *ent;
    DIR *dir = opendir(path);
    if (!dir) return NULL;

    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        } 
        ent_count++;        
    }

    char **dir_content = calloc(ent_count + 1, sizeof(char*));
    if (!dir_content) {
        handle_error("calloc error");
    }

    rewinddir(dir);

    int i = 0;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        } 
        dir_content[i] = malloc(strlen(ent->d_name) + 1);
        if (!dir_content[i]) {
            handle_error("malloc error");
        }
        strcpy(dir_content[i], ent->d_name);
        i++;
    }

    closedir(dir);
    return dir_content;
}

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

void parse_http_req(char* req_buf, http_req_t *req) {
    char *saveptr, *header_line;
    char *req_line = strtok_r(req_buf, "\n", &saveptr);
    parse_req_line(req_line, req);
    req->headers_list = NULL;

    while (1) {
        header_line = strtok_r(NULL, "\n", &saveptr);
        if (strcmp(header_line, "\r") != 0)
            parse_header_line(header_line, req);
        else break;
    }
    memset(req->body, 0, sizeof(req->body));
    char* body_tok = strtok_r(NULL, "\r", &saveptr);
    if (body_tok) strncpy(req->body, body_tok, sizeof(req->body));
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

    fclose(fd);
}

void prepare_resp_buf(char *resp_buf, http_resp_t *resp) {
    resp_buf[0] = '\0';
    char start_line[128] = {0};
    sprintf(start_line, "%s %d %s\r\n", resp->version,
            resp->status_code, resp->status_message);

    strcat(resp_buf, start_line);

    http_header_t *ent = resp->headers_list;
    for (; ent; ent = ent->next) {
        char header_line[256] = {0};
        sprintf(header_line, "%s: %s\r\n", ent->name, ent->value);
        strcat(resp_buf, header_line);
    }
    strcat(resp_buf, "\r\n");
    strcat(resp_buf, resp->body);
    printf("\r\n");
}

int handle_index_html(http_resp_t *resp, http_req_t *req) {
    char path[256] = {0};
    strcat(path, ".");
    strcat(path, req->url);
    char** dir_content = get_dir_content(path);
    for (int i = 0; dir_content[i]; i++) {
        if (strcmp(dir_content[i], "index.html") == 0 ||
            strcmp(dir_content[i], "index.php") == 0) {
            strcat(path, dir_content[i]);
            read_file(resp->body, path, MAX_BODY_SIZE);
            free_dir_content(dir_content);
            return 1;
        }
    }

    free_dir_content(dir_content);
    return 0;
}

void handle_dir_listing(http_resp_t *resp, http_req_t *req) {
    char dir_listing_html[MAX_BODY_SIZE] = {0};

    snprintf(dir_listing_html, MAX_BODY_SIZE,
        "<!DOCTYPE HTML>"
        "<html>\n"
        "<head>\n"
        "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\">\n"
        "<title>Directory listing for %s</title>\n"
        "</head>\n"
        "<body>\n"
        "<h1>Directory listing for %s</h1>\n"
        "<hr>\n"
        "<ul>\n", req->url, req->url);

    char path[256] = {0};
    strcat(path, ".");
    strcat(path, req->url);
    char** dir_content = get_dir_content(path);
    for (int i = 0; dir_content[i]; i++) {
        char ent[512] = {0};
        snprintf(ent, 512, "<li><a href=\"%s%s\">%s</a></li>\n",
                 path, dir_content[i], dir_content[i]);

        strcat(dir_listing_html, ent);
    }

    strcat(dir_listing_html, "</ul>\n</body>\n</html>\n");
    strncpy(resp->body, dir_listing_html, MAX_BODY_SIZE);

    free_dir_content(dir_content);
}

void prepare_resp(char *resp_buf, http_resp_t *resp, http_req_t *req) {
    resp->headers_list = NULL;
    memset(resp->body, '\0', sizeof(resp->body));
    resp->version = req->version;
    if (strncmp(req->method, "GET", 3) != 0) {
        resp->status_message = "Method Not Allowed";
        resp->status_code = 405;
    }
    else if (is_reg_file(req->url + 1)) {
        read_file(resp->body, req->url + 1, MAX_BODY_SIZE);
        if (strstr(req->url, ".html")) {
            add_header(&resp->headers_list, "Content-Type", "text/html");
        } else  {
            add_header(&resp->headers_list, "Content-Type", "text/plain");
        }
        resp->status_message = "OK";
        resp->status_code = 200;
    } else if (is_dir(req->url)) {
        if (!handle_index_html(resp, req)) {
            handle_dir_listing(resp, req);
        } 
        add_header(&resp->headers_list, "Content-Type", "text/html");
        resp->status_message = "OK";
        resp->status_code = 200;
    } else {
        strcpy(resp->body, "404 Not Found");
        resp->status_message = "Not Found";
        resp->status_code = 404;
    }

    char body_size_cstr[20] = {0};
    snprintf(body_size_cstr, 20, "%ld", strlen(resp->body));
    add_header(&resp->headers_list, "Content-Length", body_size_cstr);
    add_header(&resp->headers_list, "Content-Type", "text/plain");

    prepare_resp_buf(resp_buf, resp);
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
        print_http_req(&req); 

        
        http_resp_t resp;
        char resp_buf[MAX_SIZE] = {0};
        prepare_resp(resp_buf, &resp, &req);

        write(cfd, resp_buf, strlen(resp_buf));

        free_headers(req.headers_list);
        free_headers(resp.headers_list);
        close(cfd);
    }

    close(sfd);
    return 0;
}
