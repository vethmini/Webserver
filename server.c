#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFSIZE 8096
#define SRV_DIRECTORY "./"
#define PORT 8080
#define MAX_CONNECTIONS 64
#define FILE_PATH_MAX 256

struct
{
    char *ext;
    char *filetype;
} extensions[] = {
    {"html", "text/html"},
    {"jpg", "image/jpg"},
    {"jpeg", "image/jpeg"},
    {"png", "image/png"},
    {"gif", "image/gif"},
    {"ico", "image/ico"},
    {"css", "text/css"},
    {"js", "application/javascript"},
    {"zip", "image/zip"},
    {"gz", "image/gz"},
    {"tar", "image/tar"},
    {"htm", "text/html"},
    {0, 0}};

void handle_request(int client_fd, int request_count) {
    int j, file_fd, buflen;
    long i, ret, len;
    char *fstr;
    static char buffer[BUFSIZE + 1]; /* static so zero filled */

    ret = read(client_fd, buffer, BUFSIZE); /* read Web request in one go */
    if (ret == 0 || ret == -1) {
        perror("Failed to read browser request");
        close(client_fd);
        return;
    }
    if (ret > 0 && ret < BUFSIZE) /* return code is valid chars */
        buffer[ret] = 0;          /* terminate the buffer */
    else
        buffer[0] = 0;
    for (i = 0; i < ret; i++) /* remove CF and LF characters */
        if (buffer[i] == '\r' || buffer[i] == '\n')
            buffer[i] = '*';
    printf("Request %s : %d \n", buffer, request_count);
    if (strncmp(buffer, "GET ", 4) && strncmp(buffer, "get ", 4)) {
        printf("Only simple GET operation supported : %s \n", buffer);
        dprintf(client_fd, "HTTP/1.1 403 Forbidden\nContent-Length: 185\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>403 Forbidden</title>\n</head><body>\n<h1>Forbidden</h1>\nThe requested URL, file type, or operation is not allowed on this simple static file webserver.\n</body></html>\n");
        close(client_fd);
        return;
    }
    for (i = 4; i < BUFSIZE; i++) {
        if (buffer[i] == ' ') {
            buffer[i] = 0;
            break;
        }
    }
    for (j = 0; j < i - 1; j++)
        if (buffer[j] == '.' && buffer[j + 1] == '.') {
            printf("Directory . or .. is unsupported: %s \n", buffer);
            dprintf(client_fd, "HTTP/1.1 403 Forbidden\nContent-Length: 185\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>403 Forbidden</title>\n</head><body>\n<h1>Forbidden</h1>\nThe requested URL, file type, or operation is not allowed on this simple static file webserver.\n</body></html>\n");
            close(client_fd);
            return;
        }
    if (!strncmp(&buffer[0], "GET /\0", 6) || !strncmp(&buffer[0], "get /\0", 6))
        strcpy(buffer, "GET /index.html");
    buflen = strlen(buffer);
    fstr = NULL;
    for (i = 0; extensions[i].ext != 0; i++) {
        len = strlen(extensions[i].ext);
        if (!strncmp(&buffer[buflen - len], extensions[i].ext, len)) {
            fstr = extensions[i].filetype;
            break;
        }
    }
    if (fstr == NULL) {
        printf("Only simple GET operation supported : %s \n", buffer);
        dprintf(client_fd, "HTTP/1.1 403 Forbidden\nContent-Length: 185\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>403 Forbidden</title>\n</head><body>\n<h1>Forbidden</h1>\nThe requested URL, file type, or operation is not allowed on this simple static file webserver.\n</body></html>\n");
        close(client_fd);
        return;
    }

    char file_path[FILE_PATH_MAX + BUFSIZE];  // Increase buffer size
    snprintf(file_path, FILE_PATH_MAX + BUFSIZE, "%s%s", SRV_DIRECTORY, &buffer[5]);
    if ((file_fd = open(file_path, O_RDONLY)) == -1) {
        perror("Failed to open file");
        dprintf(client_fd, "HTTP/1.1 404 Not Found\nContent-Length: 136\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>404 Not Found</title>\n</head><body>\n<h1>Not Found</h1>\nThe requested URL was not found on this server.\n</body></html>\n");
        close(client_fd);
        return;
    }

    len = (long)lseek(file_fd, (off_t)0, SEEK_END);
    lseek(file_fd, (off_t)0, SEEK_SET);

    dprintf(client_fd, "HTTP/1.1 200 OK\nServer: webserver/1.0\nContent-Length: %ld\nConnection: close\nContent-Type: %s\n\n", len, fstr);

    while ((ret = read(file_fd, buffer, BUFSIZE)) > 0)
        write(client_fd, buffer, ret);

    close(file_fd);
    close(client_fd);
    exit(EXIT_SUCCESS);
}

int main() {
    int port, listenfd, client_fd, request_count;
    socklen_t length;
    static struct sockaddr_in cli_addr;
    static struct sockaddr_in serv_addr;

    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Error initializing socket");
        exit(EXIT_FAILURE);
    }

    port = PORT;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port);

    if (bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Error binding socket");
        exit(EXIT_FAILURE);
    }
    if (listen(listenfd, MAX_CONNECTIONS) < 0) {
        perror("Error listening on socket");
        exit(EXIT_FAILURE);
    }

    if (chdir(SRV_DIRECTORY) == -1) {
        perror("Can't Change to directory");
        exit(EXIT_FAILURE);
    }

    request_count = 1;
    for (;;) {
        length = sizeof(cli_addr);
        if ((client_fd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0) {
            perror("Error accepting connection");
            continue;
        }

        pid_t pid = fork();

        if (pid < 0) {
            perror("Error forking process");
            close(client_fd);
        } else if (pid == 0) {
            close(listenfd);
            handle_request(client_fd, request_count);
            exit(EXIT_SUCCESS);
        } else {
            close(client_fd);
        }

        request_count++;
    }

    return 0;
}

