#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <regex.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>
#include "protocol.h"
#include "listener_socket.h"
#include "iowrapper.h"

#undef HTTP_REGEX
#define HTTP_REGEX "HTTP/[0-9]+\\.[0-9]+"

#define LONGEST_HEADER 4096
#define MAX_BUFFER_SIZE 2048

typedef struct{
    char method[PATH_MAX];
    char uri[PATH_MAX];
    char version[PATH_MAX];
    int c_length;
    char*start;
}Important;


int in_array(char character, char*word){
    int len = strlen(word);
    int count = 0;
    for(int i =0; i<len; i++){
        if(word[i]==character){
            count = count+1;
        }
    }
    if(count>=1){
        return 0;
    }
    else{
        return -1;
    }
}



ssize_t replace_strstr(int f, char*buffer, size_t max_length, char*term){
    size_t count = 0;
    int flag = 1;
    while(count<max_length){

        ssize_t reads = read(f, buffer+count, 1);
        if(reads<=0){
            return -1;
        }
        else{
            count = count+reads;
        }

        if(count>=strlen(term)){
             flag = 1;
            for(size_t i=0; i<strlen(term); i++){
                if(buffer[count-strlen(term)+i]!=term[i]){
                     flag = 0;
                     break;
                }

            }
              if(flag==1){
                buffer[count] = '\0';
                return count;

                
            }

        }
    }
      return -1;

}

void sends(int socket, int code, char*message, char*text, int text_length){
    char h[4096];
    int len = sprintf(h,"HTTP/1.1 %d %s\r\n""Content-Length: %d\r\n""\r\n",code, message, text_length);

    if(len>0){
        write_n_bytes(socket, h, len);
    }

    if(text!=NULL && text_length>0){
        write_n_bytes(socket, text, text_length);
    }
}


void get_request(int socket, char*uri){
    if(uri[0]!='/'){
        sends(socket, 403, "Forbidden", "Forbidden\n", strlen("Forbidden\n"));
        return;
    }

    char*files = uri+1;
    if(strlen(files)==0){
        sends(socket, 403, "Forbidden", "Forbidden\n", strlen("Forbidden\n"));
        return;
    }

    struct stat s; 

    if(stat(files, &s)==-1){
        if(errno!=ENOENT){
            sends(socket, 403, "Forbidden", "Forbidden\n", strlen("Forbidden\n"));
        }
        else{
            sends(socket, 404, "Not Found", "Not Found\n", strlen("Not found\n"));
        }
        return;
    }


        if (S_ISDIR(s.st_mode)) {
        sends(socket, 403, "Forbidden", "Forbidden\n", strlen("Forbidden\n"));
        return;
    }

    int f = open(files, O_RDONLY);
    if(f<0){
        sends(socket, 403, "Forbidden", "Forbidden\n", strlen("Forbidden\n"));
        return;
    }

    sends(socket, 200, "OK", NULL, s.st_size);
    pass_n_bytes(f, socket, s.st_size);
    close(f);

}


void put_request(int socket, char*uri, Important*important){
        if (uri[0] != '/') {
        sends(socket, 403, "Forbidden", "Forbidden\n", strlen("Forbidden\n"));
        return;
    }

    char*files = uri+1;

if (strlen(files) == 0) {
        sends(socket, 403, "Forbidden", "Forbidden\n", strlen("Forbidden\n"));
        return;
    }

   struct stat check;

    int exists = (stat(files,&check)==0);

    int f = open(files, O_WRONLY|O_CREAT|O_TRUNC,0644);
    if (f<0) {
        sends(socket, 403, "Forbidden","Forbidden\n", strlen("Forbidden\n"));
        return;
    }

    ssize_t reading_bytes = pass_n_bytes(socket, f, important->c_length);
    close(f);

    if(reading_bytes<0){
        sends(socket, 403,"Forbidden","Forbidden\n", strlen("Forbidden\n"));
        return;
    }
    if(exists){
        sends(socket, 200, "OK", "OK\n", strlen("OK\n"));
    }
    else{
        sends(socket, 201, "Created", "Created\n", strlen("Created\n"));
    }

}

int parses(char*buffer, Important*important){
    regex_t re;

    char *pattern = "^(" TYPE_REGEX ") (" URI_REGEX ") (" HTTP_REGEX ")";

    if(regcomp(&re, pattern, REG_EXTENDED) != 0){
        return -1;
    }

        regmatch_t m[4];
    if (regexec(&re, buffer, 4, m, 0) != 0) {
        regfree(&re);
        return -1;
    }

   int len1 = m[1].rm_eo - m[1].rm_so;
strncpy(important->method, buffer + m[1].rm_so, len1);
important->method[len1] = '\0';  

int len2 = m[2].rm_eo - m[2].rm_so;
strncpy(important->uri, buffer + m[2].rm_so, len2);
important->uri[len2] = '\0'; 

int len3 = m[3].rm_eo - m[3].rm_so;
strncpy(important->version, buffer + m[3].rm_so, len3);
important->version[len3] = '\0';  

        regfree(&re);

    important->start = buffer+strlen(buffer);

regex_t content;
char*type = "Content-Length: *("HEADER_VALUE_REGEX")";
if (regcomp(&content, type,REG_EXTENDED) != 0) {
    return -1;
}

regmatch_t matches[2];
char *find = buffer;
int found = 0;
int c_len = -1;

while (regexec(&content, find, 2, matches, 0)==0) {
    char c_size[30];
    int len1 = matches[1].rm_eo - matches[1].rm_so;
    strncpy(c_size, find+matches[1].rm_so, len1);
    c_size[len1] = '\0';

    char *endptr;
    int val = strtol(c_size, &endptr, 10);
    if (*endptr != '\0'||val<0) {
        regfree(&content);
        return -1;
    }

    if (found == 0) {
        c_len = val;
        found = 1;
    } else {
        if (val!=c_len) {
            regfree(&content);
            return -1; 
        }
    }

    find= find+matches[0].rm_eo;
}

regfree(&content);

if (c_len>=0) {
    important->c_length = c_len;
} else {
    if(strcmp(important->method, "PUT")==0){
        return -1;
    }
    else{
        important->c_length = 0;
    }
}


for (int i=0; important->version[i]!='\0'; i++) {
    if (important->version[i] == '\r'||important->version[i] == '\n') {
        important->version[i] = '\0';
        break;
    }
}


char *begin = buffer;
    char *end = buffer+strlen(buffer);
    char*copy = begin;

      while (begin<end) {
        char *ends = NULL;

        for (copy = begin; copy<end; copy++) {
            if (copy[0] =='\r' && copy[1] =='\n') {
                ends = copy;
                break;
            }
        }

        if (ends == begin||ends==NULL) {
            break;
        }

        char line[4096];
        sprintf(line,"%.*s",(int)(ends-begin),begin);

        int flags = -1;

        if (in_array(':', line) == 0) {

            for (int i = 0; line[i] != '\0'; i++) {
                if (line[i] == ':') {
                    flags=i;
                    break;
                }
            }
            if (flags == -1){
                return -1;
            } 
            for (int i = 0; i<flags; i++) {
                if (isdigit((line[i])==0 ||isalpha(line[i]))&& line[i]!= '.'&& line[i]!= '-') {
                    return -1;
                }
            }
        }
        begin=ends+2; 
    }
    return 0;

}




int main(int argc, char *argv[]) {

    if (argc<2) {
        return 1;
    }
else{
    int p = atoi(argv[1]);
    if (p< 1||p > 65535) {
        write(STDERR_FILENO, "Invalid port\n", 14);
        return 1;
    }

    Listener_Socket_t *ls = ls_new(p);


    while (1) {
        int socket = ls_accept(ls);
        if (socket<0) {
            continue;
        }
        char buffer[LONGEST_HEADER + 1];
        memset(buffer,0, sizeof(buffer));

        ssize_t bytes_read = replace_strstr(socket, buffer, sizeof(buffer)-1, "\r\n\r\n");
        if (bytes_read <= 0) {
            sends(socket, 400, "Bad Request", "Bad Request\n", strlen("Bad Request\n"));
            close(socket);
            continue;
        }

        buffer[bytes_read] = '\0';

        Important request;
        memset(&request, 0, sizeof(request));
        int parse_result = parses(buffer, &request);
        if (parse_result==-1) {
            sends(socket, 400, "Bad Request", "Bad Request\n", strlen("Bad Request\n"));
            close(socket);
            continue;
        }

if (strcmp(request.version, "HTTP/1.0") != 0 && strcmp(request.version, HTTP_VERSION) != 0) {
    regex_t version_re;

    if (strcmp(request.version, "HTTP/1.3")==0) {
        sends(socket, 505, "Version Not Supported", "Version Not Supported\n", strlen("Version Not Supported\n"));
    } else {
        sends(socket, 400, "Bad Request", "Bad Request\n", strlen("Bad Request\n"));
    }

    regfree(&version_re);
    close(socket);
    continue;
}

    if (strcmp(request.method, "GET") == 0) {
        get_request(socket, request.uri);
    }

    else if(strcmp(request.method, "PUT") == 0){
        put_request(socket, request.uri, &request);

    }
    else{
        sends(socket, 501, "Not Implemented", "Not Implemented\n", strlen("Not Implemented\n"));
    }

    close(socket);
    }

    ls_delete(&ls);
    }
    return 0;
}
