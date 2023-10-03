#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#define _GNU_SOURCE

#define PORT 2525
#define BUFFER_SIZE 1024

typedef struct {
  char *from;
  char *to;
  char *body;
} email;

// Caller is responsible for freeing memory here
char *email_str(email *email_ptr) {
  /* printf("from size: %d\n", strlen(email_ptr->from)); */
  /* printf("to size: %d\n", strlen(email_ptr->to)); */
  /* printf("body size: %d\n", strlen(email_ptr->body)); */
  char *buff = (char *)malloc(strlen(email_ptr->from) + strlen(email_ptr->to) + strlen(email_ptr->body) + 19);
  sprintf(buff, "From: %s\nTo:%s\nBody:\n%s\n", email_ptr->from,
                      email_ptr->to, email_ptr->body);
  return buff;
}

int email_free(email *email_ptr) {
  free(email_ptr->from);
  free(email_ptr->to);
  free(email_ptr->body);
  return 0;
}

int server_socket;

void signal_handler(int sig) {
  printf("\nReceived signal %d. Closing the socket.\n", sig);
  close(server_socket);
  exit(EXIT_SUCCESS);
}

void handle_ehlo(int client_socket) {
  char response[BUFFER_SIZE];
  snprintf(response, BUFFER_SIZE, "250 smtp.example.com\r\n");
  send(client_socket, response, strlen(response), 0);
}

void handle_mail_from(int client_socket, char *from_address, email *email_ptr) {
  // Validate email
  email_ptr->from = (char *)malloc(strlen(from_address) - 1);
  if (email_ptr->from == NULL) {
    fprintf(stderr, "Memory allocation failed\n");
    close(client_socket);
    return;
  }
  strncpy(email_ptr->from, from_address, strlen(from_address) - 2);
  email_ptr->from[strlen(from_address)-2] = '\0';
  // Respond with 250 OK
  send(client_socket, "250 OK\r\n", 8, 0);
}

void handle_mail_to(int client_socket, char *to_address, email *email_ptr) {
  // Validate email, and make sure we have this user
  email_ptr->to = (char *)malloc(strlen(to_address) - 1);
  strncpy(email_ptr->to, to_address, strlen(to_address) - 2);
  email_ptr->to[strlen(to_address)-2] = '\0';
  // Respond with 250 OK
  send(client_socket, "250 OK\r\n", 8, 0);
}

void handle_data(int client_socket, email* email_ptr) {
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
        strncmp(message_content + total_bytes_received - 5, "\r\n.\r\n", 5) ==
            0) {
      // Message content ends with a single period
      // Handle email here, db, or otherwise
      total_bytes_received -= 5;
      email_ptr->body = (char *)malloc(total_bytes_received + 1);
      strncpy(email_ptr->body, message_content, total_bytes_received);
      email_ptr->body[total_bytes_received] = '\0';
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
  sqlite3 *db;
  sqlite3_stmt *res;

  int rc = sqlite3_open(":memory:", &db);

  if (rc != SQLITE_OK) {
    fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);

    return 1;
  }

  rc = sqlite3_prepare_v2(db, "SELECT SQLITE_VERSION()", -1, &res, 0);

  if (rc != SQLITE_OK) {
    fprintf(stderr, "Failed to fetch data: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);

    return 1;
  }

  rc = sqlite3_step(res);

  if (rc == SQLITE_ROW) {
    printf("%s\n", sqlite3_column_text(res, 0));
  }

  sqlite3_finalize(res);
  sqlite3_close(db);

  int client_socket;
  struct sockaddr_in server_addr;
  struct sockaddr_in client_addr;
  char buffer[BUFFER_SIZE];

  if (signal(SIGINT, signal_handler) == SIG_ERR) {
    perror("Unable to set up signal handler");
    return EXIT_FAILURE;
  }

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

    // Send client a greeting
    send(client_socket, "220 Welcome to my custom SMTP server\r\n", 38, 0);
    email current_email;
    memset(&current_email, 0, sizeof(email));

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
        handle_mail_from(client_socket, from_address, &current_email);
      } else if (strncmp(buffer, "RCPT TO:", 8) == 0) {
        char *to_address = buffer + 8;
        handle_mail_to(client_socket, to_address, &current_email);
      } else if (strncmp(buffer, "DATA", 4) == 0) {
        send(client_socket, "354 End data with <CR><LF>.<CR><LF>\r\n", 37, 0);
        handle_data(client_socket, &current_email);
      } else if (strncmp(buffer, "QUIT", 4) == 0) {
        send(client_socket, "221 Goodbye\r\n", 14, 0);
        char *email_string = email_str(&current_email);
        printf("%s\n", email_string);
        free(email_string);
        if (email_free(&current_email)) {
          fprintf(stderr, "Failed to free email struct\n");
        }
        close(client_socket);
        break;
      }
    }
  }

  close(server_socket);

  return 0;
}
