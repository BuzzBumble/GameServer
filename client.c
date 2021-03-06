#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#define PORT 2034
#define MAXLINE 1024
int main()
{
    int sockfd;
    char* message = "Hello Server";
    struct sockaddr_in servaddr;
  
    int n, len;
    // Creating socket file descriptor
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("socket creation failed");
        exit(0);
    }
  
    memset(&servaddr, 0, sizeof(servaddr));
  
    // Filling server information
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = PORT;

    if(inet_pton(AF_INET, "192.168.1.72", &servaddr.sin_addr)<=0)
    {
        perror("inet_pton()");
        return 1;
    }
  
    if (connect(sockfd, (struct sockaddr*)&servaddr, 
                             sizeof(servaddr)) < 0) {
        perror("connect()");
        exit(1);
    }

    puts("Connected to server");

    uint32_t cfd = 0;
    uint8_t type = 1;
    uint8_t context = 1;
    uint8_t plen = 2;
    uint32_t payload = htonl(0x0102) >> 16;

    puts("writing data");
    write(sockfd, &cfd, sizeof(cfd));
    write(sockfd, &type, sizeof(type));
    write(sockfd, &context, sizeof(context));
    write(sockfd, &plen, sizeof(plen));
    if (plen > 0) {
        write(sockfd, &payload, plen);
    }

    puts("Wrote all data");

    read(sockfd, &type, sizeof(type));
    read(sockfd, &context, sizeof(context));
    read(sockfd, &plen, sizeof(plen));
    if (plen > 0) {
        read(sockfd, &payload, plen);
    }

    cfd = payload;

    puts("Response Received: ");
    printf("New Client fd: %d\n", cfd);
    printf("Type: %d\n", type);
    printf("Context: %d\n", context);
    printf("Payload Length: %d\n", plen);
    printf("Payload: %X\n", payload);

    for (;;) {}

    close(sockfd);
}
