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
  char *buff;
  asprintf(&buff, "From: %s\nTo:%s\nBody:\n%s\n", email_ptr->from,
           email_ptr->to, email_ptr->body);
  return buff;
}

void email_free(email *email_ptr) {
  free(email_ptr->from);
  free(email_ptr->to);
  free(email_ptr->body);
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

void handle_mail_from(int client_socket, const char *from_address,
                      email *email_ptr) {
  // Validate email
  email_ptr->from = (char *)malloc(strlen(from_address) - 1);
  if (email_ptr->from == NULL) {
    fprintf(stderr, "Memory allocation failed\n");
    close(client_socket);
    return;
  }
  strncpy(email_ptr->from, from_address, strlen(from_address) - 2);
  email_ptr->from[strlen(from_address) - 2] = '\0';
  // Respond with 250 OK
  send(client_socket, "250 OK\r\n", 8, 0);
}

void handle_mail_to(int client_socket, const char *to_address, email *email_ptr,
                    sqlite3 *db) {
  email_ptr->to = (char *)malloc(strlen(to_address) - 1);
  strncpy(email_ptr->to, to_address, strlen(to_address) - 2);
  email_ptr->to[strlen(to_address) - 2] = '\0';

  // Validate email, and make sure we have this user
  sqlite3_stmt *find_user;
  int code;
  const char *query = "SELECT 1 FROM users WHERE username = ?";
  code = sqlite3_prepare_v2(db, query, -1, &find_user, 0);
  if (code != SQLITE_OK) {
    fprintf(stderr, "Failed to prepare in 'handle_mail_to': %s\n",
            sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
  }
  code = sqlite3_bind_text(find_user, 1, email_ptr->to, -1, 0);
  if (code != SQLITE_OK) {
    fprintf(stderr, "Failed to bind in 'handle_mail_to': %s\n",
            sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
  }
  code = sqlite3_step(find_user);
  if (code == SQLITE_ROW) {
    // Respond with 250 OK
    send(client_socket, "250 OK\r\n", 8, 0);
  } else {
    // User does not exist
    send(client_socket, "550 5.1.1 User unknown; Invalid recipient address\r\n", 51, 0);
    close(client_socket);
  }
}

void handle_data(int client_socket, email *email_ptr) {
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

  int rc = sqlite3_open("email.db", &db);

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

  // Setting up our tables (Users, Emails)
  sqlite3_stmt *users_stmt;
  const char *users = "CREATE TABLE IF NOT EXISTS users(\
id INTEGER PRIMARY KEY,\
username TEXT UNIQUE,\
password TEXT UNIQUE\
)";
  int code = sqlite3_prepare_v2(db, users, strlen(users), &users_stmt, 0);
  if (code != SQLITE_OK) {
    fprintf(stderr, "Failed to prepare create table 'users': %s\n",
            sqlite3_errmsg(db));
    sqlite3_close(db);
    return 1;
  }
  code = sqlite3_step(users_stmt);
  if (code != SQLITE_DONE) {
    fprintf(stderr, "Failed to create table 'users': %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return 1;
  }
  sqlite3_finalize(users_stmt);

  sqlite3_stmt *emails_stmt;
  const char *emails = "CREATE TABLE IF NOT EXISTS emails(\
id INTEGER PRIMARY KEY,\
mail_from TEXT,\
rcpt_to TEXT,\
body TEXT,\
user_id INTEGER\
)";
  code = sqlite3_prepare_v2(db, emails, strlen(emails), &emails_stmt, 0);
  if (code != SQLITE_OK) {
    fprintf(stderr, "Failed to prepare create table 'emails': %s\n",
            sqlite3_errmsg(db));
    sqlite3_close(db);
    return 1;
  }
  code = sqlite3_step(emails_stmt);
  if (code != SQLITE_DONE) {
    fprintf(stderr, "Failed to create table 'emails': %s\n",
            sqlite3_errmsg(db));
    sqlite3_close(db);
    return 1;
  }
  sqlite3_finalize(emails_stmt);

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
        const char *from_address = buffer + 10;
        handle_mail_from(client_socket, from_address, &current_email);
      } else if (strncmp(buffer, "RCPT TO:", 8) == 0) {
        const char *to_address = buffer + 8;
        // Need to handle the socket and email cleanup here if user DNE
        handle_mail_to(client_socket, to_address, &current_email, db);
      } else if (strncmp(buffer, "DATA", 4) == 0) {
        send(client_socket, "354 End data with <CR><LF>.<CR><LF>\r\n", 37, 0);
        handle_data(client_socket, &current_email);
      } else if (strncmp(buffer, "QUIT", 4) == 0) {
        send(client_socket, "221 Goodbye\r\n", 14, 0);

        // Debugging line
        char *email_string = email_str(&current_email);
        printf("----Email Content----\n%s\n", email_string);
        free(email_string);

        // Insert email into db, TODO check if rcpt exists
        sqlite3_stmt *email;
        const char *insert_email = "INSERT INTO emails (mail_from, rcpt_to, "
                                   "body, user_id) VALUES (?, ?, ?, ?)";
        int code = sqlite3_prepare_v2(db, insert_email, -1, &email, 0);
        if (code != SQLITE_OK) {
          fprintf(stderr,
                  "Failed to prepare the insert email into 'emails': %s\n",
                  sqlite3_errmsg(db));
          sqlite3_close(db);
          return 1;
        }
        code = sqlite3_bind_text(email, 1, current_email.from, -1, 0);
        code = sqlite3_bind_text(email, 2, current_email.to, -1, 0);
        code = sqlite3_bind_text(email, 3, current_email.body, -1, 0);
        if (code != SQLITE_OK) {
          fprintf(stderr, "Failed to bind params for email into 'emails': %s\n",
                  sqlite3_errmsg(db));
          sqlite3_close(db);
          return 1;
        }

        code = sqlite3_step(email);
        if (code != SQLITE_DONE) {
          fprintf(stderr, "Failed to insert email into 'emails': %s\n",
                  sqlite3_errmsg(db));
          sqlite3_close(db);
          return 1;
        }
        sqlite3_finalize(email);

        email_free(&current_email);
        close(client_socket);
        break;
      }
    }
  }

  close(server_socket);
  sqlite3_close(db);

  return 0;
}
