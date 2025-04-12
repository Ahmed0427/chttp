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
#include <linux/limits.h>
#include <time.h>

#define handle_error(msg) \
do { perror(msg); exit(EXIT_FAILURE); } while (0)

#define MAX_SIZE 1024 * 12
#define MAX_BODY_SIZE 1024 * 10
#define LISTEN_BACKLOG 50

char CUR_DIR[PATH_MAX] = {0};

typedef struct http_header_t {
    char *name;          
    char *value;         
    struct http_header_t *next; 
} http_header_t;

typedef struct {
    char *method;     
    char path[PATH_MAX];         
    char *version;      
    http_header_t *headers_list; 
    char body[MAX_BODY_SIZE];          
} http_req_t;

typedef struct {
    char *version;        
    short status_code;         
    char *status_message; 
    http_header_t *headers_list; 
    char *body;          
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

int setCWD() {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        strcpy(CUR_DIR, cwd);
    } else {
        return -1;
    }
    return 0;
}

void free_dir_content(char** dir_content) {
    if (!dir_content) return;
    
    int j;
    for (j = 0; dir_content[j]; j++) {
        free(dir_content[j]); 
    }
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
    if (dir_content == NULL) {
        closedir(dir);
        handle_error("calloc error");
    }
    
    rewinddir(dir);
    int i = 0;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        } 
        dir_content[i] = calloc(1, strlen(ent->d_name) + 2);
        if (!dir_content[i]) {
            free_dir_content(dir_content);
            closedir(dir);
            handle_error("malloc error");
        }
        strcpy(dir_content[i], ent->d_name);
        if (ent->d_type == DT_DIR) strcat(dir_content[i], "/");
        if (++i == ent_count) {
            break;
        }
    }
    closedir(dir);
    return dir_content;
}

void free_headers(http_header_t *headers) {
    http_header_t *ent = headers;
    http_header_t *next;
    
    while (ent) {
        next = ent->next;
        free(ent->name);
        free(ent->value);
        free(ent);
        ent = next;
    }
}

void free_resp(http_resp_t *resp) {
    free_headers(resp->headers_list);
    free(resp->version);
    free(resp->body);
}

void free_req(http_req_t *req) {
    free(req->method);
    free(req->version);
    free_headers(req->headers_list);
}

void print_full_req(http_req_t *req) {
    printf("%s %s %s\r\n", req->method, req->path, req->version);
    http_header_t *ent = req->headers_list;
    for (; ent; ent = ent->next) {
        printf("%s: %s\r\n", ent->name, ent->value);
    }
    if (req->body) {
        printf("\r\n%s", req->body);
    }
    printf("\r\n");
}

void print_req(http_req_t *req) {
    time_t mytime = time(NULL);
    char * time_str = ctime(&mytime);
    time_str[strlen(time_str)-1] = '\0';
    printf("\n[%s]  %s %s %s\r\n", time_str,
           req->method, req->path, req->version);
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
    req->method = strdup(strtok_r(req_line, " ", &saveptr));
    if (!req->method) handle_error("strdup error");
    
    char *path = strtok_r(NULL, " ", &saveptr);
    if (path) strcpy(req->path, path);
    
    req->version = strdup(strtok_r(NULL, "\r", &saveptr));
    if (!req->version) handle_error("strdup error");
}

void add_header(http_header_t **headers_list, char* name, char* value) {
    http_header_t *ent = malloc(sizeof(http_header_t));
    if (ent == NULL) {
        handle_error("malloc error");
    }
    
    ent->name = strdup(name);
    if (!ent->name) {
        free(ent);
        handle_error("strdup error");
    }
    
    ent->value = strdup(value);
    if (!ent->value) {
        free(ent->name);
        free(ent);
        handle_error("strdup error");
    }
    
    ent->next = *headers_list;
    *headers_list = ent;
}

void parse_header_line(char* header_line, http_req_t *req) { 
    char *colon = strchr(header_line, ':');
    if (!colon) return;
    
    *colon = '\0';
    char *name = header_line;
    char *value = colon + 1;
    
    while (*value == ' ') value++;
    
    char *cr = strchr(value, '\r');
    if (cr) *cr = '\0';
    
    add_header(&req->headers_list, name, value);
}

void parse_http_req(char* req_buf, http_req_t *req) {
    char *saveptr, *line;
    char *req_copy = strdup(req_buf);
    if (!req_copy) handle_error("strdup error");
    
    line = strtok_r(req_copy, "\n", &saveptr);
    if (line) {
        parse_req_line(line, req);
        req->headers_list = NULL;
        
        while ((line = strtok_r(NULL, "\n", &saveptr)) != NULL) {
            if (line[0] == '\r' || line[0] == '\0') {
                break;
            }
            parse_header_line(line, req);
        }
        
        char *body = strtok_r(NULL, "", &saveptr);
        memset(req->body, 0, sizeof(req->body));
        if (body) {
            strncpy(req->body, body, sizeof(req->body) - 1);
        }
    }
    
    free(req_copy);
}

char* read_file(char *path) {
    FILE* fd = fopen(path, "rb");
    if (!fd) {
        char* empty = strdup("");
        if (!empty) handle_error("strdup error");
        return empty;
    }
    
    fseek(fd, 0L, SEEK_END);
    size_t file_size = ftell(fd);
    rewind(fd);

    char* buffer = calloc(file_size + 1, 1);
    if (!buffer) {
        fclose(fd);
        handle_error("calloc error");
    }
    
    size_t bytes_read = fread(buffer, 1, file_size, fd);
    if (bytes_read < file_size) {
        if (ferror(fd)) {
            free(buffer);
            fclose(fd);
            return NULL;
        }
    }
    buffer[bytes_read] = '\0';
    
    fclose(fd);
    return buffer;
}

int handle_index_html(http_resp_t *resp, http_req_t *req) {
    char path[PATH_MAX] = {0};
    strcpy(path, CUR_DIR);
    strcat(path, req->path);
    
    if (path[strlen(path) - 1] != '/') {
        strcat(path, "/");
    }
    
    char** dir_content = get_dir_content(path);
    if (!dir_content) return 0;
    
    char full_path[PATH_MAX];
    int found = 0;
    
    for (int i = 0; dir_content[i]; i++) {
        if (strcmp(dir_content[i], "index.html") == 0 ||
            strcmp(dir_content[i], "index.php") == 0) {
            strcpy(full_path, path);
            strcat(full_path, dir_content[i]);
            resp->body = read_file(full_path);
            if (!resp->body) return 0;
            found = 1;
            break;
        }
    }
    
    free_dir_content(dir_content);
    return found;
}

int handle_dir_listing(http_resp_t *resp, http_req_t *req) {
    size_t initial_size = 1024;
    resp->body = calloc(initial_size, 1);
    if (!resp->body) return -1;
    
    int written = snprintf(resp->body, initial_size,
        "<!DOCTYPE HTML>\n"
        "<html>\n"
        "<head>\n"
        "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\">\n"
        "<title>Directory listing for %s</title>\n"
        "</head>\n"
        "<body>\n"
        "<h1>Directory listing for %s</h1>\n"
        "<hr>\n"
        "<ul>\n", req->path, req->path);
    
    char path[PATH_MAX] = {0};
    strcpy(path, CUR_DIR);
    strcat(path, req->path);
    
    if (path[strlen(path) - 1] != '/') {
        strcat(path, "/");
    }
    
    char** dir_content = get_dir_content(path);
    
    if (dir_content) {
        for (int i = 0; dir_content[i]; i++) {
            // Create proper URL path
            char url_path[PATH_MAX] = {0};
            strcpy(url_path, req->path);
            
            if (url_path[strlen(url_path) - 1] != '/') {
                strcat(url_path, "/");
            }
            strcat(url_path, dir_content[i]);
            
            char ent[5120] = {0};
            snprintf(ent, sizeof(ent), "<li><a href=\"%s\">%s</a></li>\n",
                     url_path, dir_content[i]);
            
            if (written + strlen(ent) + 50 >= initial_size) {
                initial_size *= 2;
                char* new_body = realloc(resp->body, initial_size);
                if (!new_body) {
                    free_req(req);
                    free_resp(resp);
                    free_dir_content(dir_content);
                    return -1;
                }
                resp->body = new_body;
            }
            
            strcat(resp->body, ent);
            written += strlen(ent);
        }
        free_dir_content(dir_content);
    } else {
        strcat(resp->body, "<li>Error reading directory</li>\n");
        written += 35;
    }
    
    if (written + 50 >= (long)initial_size) {
        initial_size += 100;
        char* new_body = realloc(resp->body, initial_size);
        if (!new_body) {
            free(resp->body);
            return -1;
        }
        resp->body = new_body;
    }
    
    strcat(resp->body, "</ul>\n</body>\n</html>\n");
    return 0;
}

char* prepare_resp_buf(http_resp_t *resp) {
    if (!resp) return NULL;
    
    size_t header_size = 128; 
    http_header_t *ent = resp->headers_list;
    for (; ent; ent = ent->next) {
        header_size += strlen(ent->name) + 2 + strlen(ent->value) + 2;
    }
    
    size_t body_size = resp->body ? strlen(resp->body) : 0;
    size_t total_size = header_size + 2 + body_size + 1;
    
    char *resp_buf = calloc(total_size, 1);
    if (!resp_buf) return NULL;
    
    char *ptr = resp_buf;
    
    int bytes_written = snprintf(ptr, total_size, "%s %d %s\r\n", 
                              resp->version, resp->status_code, resp->status_message);
    if (bytes_written < 0) {
        free(resp_buf);
        return NULL;
    }

    ptr += bytes_written;
    size_t remaining = total_size - bytes_written;
    
    ent = resp->headers_list;
    for (; ent; ent = ent->next) {
        bytes_written = snprintf(ptr, remaining, "%s: %s\r\n", ent->name, ent->value);
        if (bytes_written < 0 || bytes_written >= (int)remaining) {
            free(resp_buf);
            return NULL;
        }
        ptr += bytes_written;
        remaining -= bytes_written;
    }
    
    bytes_written = snprintf(ptr, remaining, "\r\n");
    if (bytes_written < 0 || bytes_written >= (int)remaining) {
        free(resp_buf);
        return NULL;
    }
    ptr += bytes_written;
    remaining -= bytes_written;
    
    if (resp->body && body_size > 0) {
        bytes_written = snprintf(ptr, remaining, "%s", resp->body);
        if (bytes_written < 0 || bytes_written >= (int)remaining) {
            free(resp_buf);
            return NULL;
        }
    }
    
    return resp_buf;
}

char* prepare_resp(http_resp_t *resp, http_req_t *req) {
    resp->headers_list = NULL;
    resp->version = strdup(req->version);
    if (!resp->version) handle_error("strdup error");
    
    resp->body = strdup("");
    if (!resp->body) handle_error("strdup error");
    
    char* content_type = "text/plain";
    
    char path[PATH_MAX] = {0};
    strcpy(path, CUR_DIR);
    strcat(path, req->path);
    
    char clean_path[PATH_MAX];
    strcpy(clean_path, path);
    
    if (strncmp(req->method, "GET", 3) != 0) {
        resp->status_message = "Method Not Allowed";
        resp->status_code = 405;
        
        free(resp->body);
        resp->body = strdup("405 Method Not Allowed");
        if (!resp->body) handle_error("strdup error");
    }
    else if (is_reg_file(clean_path)) {
        free(resp->body);
        resp->body = read_file(clean_path);
        if (strstr(req->path, ".html")) {
            content_type = "text/html";
        } else if (strstr(req->path, ".css")) {
            content_type = "text/css";
        } else {
            content_type = "text/plain";
        }
        if (resp->body == NULL) {
            resp->body = strdup("404 Not Found");
            if (!resp->body) handle_error("strdup error");
            resp->status_message = "Not Found";
            resp->status_code = 404;
        } else {
            resp->status_message = "OK";
            resp->status_code = 200;
        }
    } else if (is_dir(path)) {
        free(resp->body);
        if (!handle_index_html(resp, req)) {
            if (handle_dir_listing(resp, req) == -1) {
                resp->body = strdup("404 Not Found");
                if (!resp->body) handle_error("strdup error");
                resp->status_message = "Not Found";
                resp->status_code = 404;
            }
        } 
        content_type = "text/html";
        resp->status_message = "OK";
        resp->status_code = 200;
    } else {
        free(resp->body);
        resp->body = strdup("404 Not Found");
        if (!resp->body) handle_error("strdup error");
        resp->status_message = "Not Found";
        resp->status_code = 404;
    }
    
    char body_size_cstr[20] = {0};
    snprintf(body_size_cstr, sizeof(body_size_cstr), "%ld", strlen(resp->body));
    add_header(&resp->headers_list, "Content-Length", body_size_cstr);
    add_header(&resp->headers_list, "Content-Type", content_type);
    char *resp_buf = prepare_resp_buf(resp);
    if (!resp_buf) return strdup("");
    return resp_buf;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <port> <directory>\n", argv[0]);
        exit(1);
    } else {
        strcpy(CUR_DIR, argv[2]);
        if (!is_dir(CUR_DIR)) {
            handle_error("argv[2]");
        }
    }
    
    const int PORT = atoi(argv[1]);
    if (PORT == 0) {
        handle_error("not a valid port");
    }
    
    int reuse = 1; 
    int sfd;
    sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) {
        handle_error("socket error");
    }
    
    if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        handle_error("setsockopt error");
    }
    
    struct sockaddr_in saddr, caddr;
    socklen_t client_addrlen = sizeof(caddr);
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(PORT);
    inet_pton(AF_INET, "0.0.0.0", &saddr.sin_addr);
    
    if (bind(sfd, (struct sockaddr*)&saddr, sizeof(saddr)) == -1) {
        handle_error("bind error");
    }
    
    if (listen(sfd, LISTEN_BACKLOG) == -1) {
        handle_error("listen error");
    }
    
    printf("serving HTTP on port %d, root directory: %s\n", PORT, CUR_DIR);
    
    while (1) {
        memset(&caddr, 0, sizeof(caddr));
        int cfd = accept(sfd, (struct sockaddr*)&caddr, &client_addrlen);
        if (cfd == -1) {
            perror("accept error");
            continue;
        }
        
        char req_buf[MAX_SIZE] = {0};
        ssize_t bytes_read = read(cfd, req_buf, MAX_SIZE - 1);
        if (bytes_read <= 0) {
            close(cfd);
            continue;
        }
        req_buf[bytes_read] = '\0';
        
        http_req_t req;
        memset(&req, 0, sizeof(http_req_t));
        parse_http_req(req_buf, &req); 
        print_req(&req); 
        
        http_resp_t resp;
        memset(&resp, 0, sizeof(http_resp_t));
        char *resp_buf = prepare_resp(&resp, &req);
        
        write(cfd, resp_buf, strlen(resp_buf));
        
        free(resp_buf);
        free_resp(&resp);
        free_req(&req);

        close(cfd);
    }
    
    close(sfd);
    return 0;
}
