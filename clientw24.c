#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define MAX_COMMAND_LEN 256
#define MAX_RESPONSE_LEN 1024

void sendCommand(int serverSocket, const char *command) {
    send(serverSocket, command, strlen(command), 0);
}

void receiveResponse(int serverSocket, char *response) {
    ssize_t bytesRead = recv(serverSocket, response, MAX_RESPONSE_LEN, 0);
    if (bytesRead == -1) {
        perror("Receive error");
        exit(EXIT_FAILURE);
    }
    response[bytesRead] = '\0'; // Ensure null-terminated string
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <server_ip> <server_port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *serverIp = argv[1];
    int serverPort = atoi(argv[2]);

    // Create socket
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Configure server address
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(serverPort);
    if (inet_pton(AF_INET, serverIp, &serverAddr.sin_addr) <= 0) {
        perror("Invalid server IP address");
        exit(EXIT_FAILURE);
    }

    // Connect to server
    if (connect(serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == -1) {
        perror("Connection failed");
        exit(EXIT_FAILURE);
    }

    printf("Connected to server %s:%d\n", serverIp, serverPort);

    char command[MAX_COMMAND_LEN];
    char response[MAX_RESPONSE_LEN];

    while (1) {
        printf("Enter command : ");
        fgets(command, MAX_COMMAND_LEN, stdin);
        command[strcspn(command, "\n")] = '\0'; // Remove newline character

        if (strcmp(command, "quitc") == 0) {
            sendCommand(serverSocket, command);
            break; // Exit loop if quit command is sent
        }

        sendCommand(serverSocket, command);
        receiveResponse(serverSocket, response);
        printf("Server response:\n%s\n", response);
    }

    // Close socket
    close(serverSocket);

    return 0;
}

