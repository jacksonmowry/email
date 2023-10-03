#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define SERVER_ADDRESS "127.0.0.1"
#define SERVER_PORT 2525
#define BUFFER_SIZE 1024

void send_command(int sockfd, const char *command) {
    send(sockfd, command, strlen(command), 0);
    printf("Sent: %s", command);
}

int main() {
    int sockfd;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE] = {0};

    // Create a socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed");
        return -1;
    }

    // Configure the server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    // Convert IPv4 and IPv6 addresses from text to binary form
    if (inet_pton(AF_INET, SERVER_ADDRESS, &server_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        return -1;
    }

    // Connect to the server
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        return -1;
    }

    // Receive the welcome message
    recv(sockfd, buffer, BUFFER_SIZE, 0);
    printf("Received: %s", buffer);

    // EHLO command
    send_command(sockfd, "EHLO example.com\r\n");
    recv(sockfd, buffer, BUFFER_SIZE, 0);
    printf("Received: %s", buffer);

    // MAIL FROM command
    send_command(sockfd, "MAIL FROM: <from@example.com>\r\n");
    recv(sockfd, buffer, BUFFER_SIZE, 0);
    printf("Received: %s", buffer);

    // RCPT TO command
    send_command(sockfd, "RCPT TO: <to@example.com>\r\n");
    recv(sockfd, buffer, BUFFER_SIZE, 0);
    printf("Received: %s", buffer);

    // DATA command
    send_command(sockfd, "DATA\r\n");
    recv(sockfd, buffer, BUFFER_SIZE, 0);
    printf("Received: %s", buffer);

    // Email content
    const char *email_content = "From: from@example.com\r\n"
                                 "To: to@example.com\r\n"
                                 "Subject: Test Email\r\n"
                                 "\r\n"
                                 "This is a test email.\r\n.\r\n";

    // Send email content
    send(sockfd, email_content, strlen(email_content), 0);

    // Receive the response for the email content
    recv(sockfd, buffer, BUFFER_SIZE, 0);
    printf("Received: %s", buffer);

    // QUIT command
    send_command(sockfd, "QUIT\r\n");
    recv(sockfd, buffer, BUFFER_SIZE, 0);
    printf("Received: %s", buffer);

    // Close the socket
    close(sockfd);

    return 0;
}
