#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 2525
#define BUFFER_SIZE 1024

void handle_ehlo(int client_socket) {
  char response[BUFFER_SIZE];
  snprintf(response, BUFFER_SIZE,
           "250-smtp.example.com\r\n250-PIPELINING\r\n250-SIZE "
           "10240000\r\n250-AUTH PLAIN LOGIN\r\n250 OK\r\n");
  send(client_socket, response, strlen(response), 0);
}

void handle_mail_from(int client_socket, char *from_address) {
  // Validate email

  // Respond with 250 OK
  send(client_socket, "250 OK\r\n", 8, 0);
}

void handle_mail_to(int client_socket, char *to_address) {
  // Validate email, and make sure we have this user

  // Respond with 250 OK
  send(client_socket, "250 OK\r\n", 8, 0);
}

void handle_data(int client_socket) {
  char message_content[BUFFER_SIZE];
  int total_bytes_received = 0;

  // continue until our stop signal
  while (1) {
    int bytes_received =
        recv(client_socket, message_content + total_bytes_received,
             BUFFER_SIZE - total_bytes_received, 0);
    if (bytes_received == -1) {
      perror("Receive in handle_data failed");
      close(client_socket);
      return;
    }

    total_bytes_received += bytes_received;

    if (total_bytes_received >= 5 &&
        strcmp(message_content + total_bytes_received - 5, "\r\n.\r\n") == 0) {
      // Message content ends with a single period
      // Handle email here, db, or otherwise
      total_bytes_received -= 5;
      printf("Received message content:\n%.*s\n", total_bytes_received,
             message_content);
      // Respond with 250 OK
      send(client_socket, "250 OK\r\n", 8, 0);
      break;
    } else if (total_bytes_received >= 3 &&
               strcmp(message_content + total_bytes_received - 3, "\r\n.") ==
                   0) {
      // Received a line with a single period
      total_bytes_received -= 3;
      printf("Received message content:\n%.*s\n", total_bytes_received,
             message_content);
      // Respond with 250 OK
      send(client_socket, "250 OK\r\n", 8, 0);
      break;
    }
  }
}

int main() {
  int server_socket;
  int client_socket;
  struct sockaddr_in server_addr;
  struct sockaddr_in client_addr;
  char buffer[BUFFER_SIZE];

  // Create our socket
  server_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (server_socket == -1) {
    perror("Socket creation failed");
    exit(EXIT_FAILURE);
  }

  // Init server addres struct
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  server_addr.sin_port = htons(PORT);

  // Bind socket
  if (bind(server_socket, (struct sockaddr *)&server_addr,
           sizeof(server_addr)) == -1) {
    perror("Socket binding failed");
    exit(EXIT_FAILURE);
  }

  // Listen
  if (listen(server_socket, 5) == -1) {
    perror("Listen failed");
    exit(EXIT_FAILURE);
  }

  printf("Server listening on port %d\n", PORT);

  while (1) {
    puts("Waiting for a connection...");
    // Accept conn
    int addr_len = sizeof(client_addr);
    client_socket = accept(server_socket, (struct sockaddr *)&client_addr,
                           (socklen_t *)&addr_len);
    if (client_socket == -1) {
      perror("Accept failed");
      exit(EXIT_FAILURE);
    }

    printf("Connection accepted. Client socket: %d\n", client_socket);

    while (1) {
      memset(buffer, 0, BUFFER_SIZE);

      int bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0);

      if (bytes_received == 0) {
        // Client disconnect
        printf("Client disconnected. Closing socket: %d\n", client_socket);
        close(client_socket);
        break;
      } else if (bytes_received == -1) {
        perror("Receive failed");
        close(client_socket);
        break;
      }

      printf("Received %d bytes: %s\n", bytes_received, buffer);

      // Process the received command
      if (strncmp(buffer, "EHLO", 4) == 0) {
        handle_ehlo(client_socket);
      } else if (strncmp(buffer, "MAIL FROM:", 10) == 0) {
        char *from_address = buffer + 10;
        handle_mail_from(client_socket, from_address);
      } else if (strncmp(buffer, "MAIL TO:", 8) == 0) {
        char *to_address = buffer + 8;
        handle_mail_to(client_socket, to_address);
      } else if (strncmp(buffer, "DATA", 4) == 0) {
        handle_data(client_socket);
      } else if (strncmp(buffer, "QUIT", 4) == 0) {
        send(client_socket, "221 Goodbye\r\n", 14, 0);
        close(client_socket);
      }
    }
  }

  close(server_socket);

  return 0;
}
