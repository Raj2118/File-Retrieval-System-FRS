//Included all necessary libraries for project...
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <archive.h>
#include <archive_entry.h>
#include <pthread.h>

//Define port numbers for mirrors and maximum limits for directories, buffer sizes, and path lengths.
#define MAX_DIRS 100

#define MAX_BUFFER_SIZE 1024
#define MAX_PATH_LEN 256

#define MIRROR2_PORT 9091
#define MAX_CLIENTS 3



//Using send system call to send a message to client with length of message.
void sendResponse(int clientSocket, const char *response) {
    send(clientSocket, response, strlen(response), 0);
}


//dirlist -a (compare each word with another and sort..)
int compareNames(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

//Compare creating of time for each file and folder ans sort accordingly with using stat(retrieve file statistics).
int compareCreationTime(const void *a, const void *b) {
    const char *nameA = *(const char **)a;
    const char *nameB = *(const char **)b;

    struct stat statA, statB;
    if (stat(nameA, &statA) != 0 || stat(nameB, &statB) != 0) {
        return 0; // Default to no change if stat fails
    }

    // Compare by creation time (st_ctime) -1 indicates File A created earlier and -1 for later.
    if (statA.st_ctime < statB.st_ctime) return -1;
    else if (statA.st_ctime > statB.st_ctime) return 1;
    else return 0;
}


void listDirectories(int clientSocket, const char *option) {
    // Getting the home directory path from environment variables
    const char *homeDir = getenv("HOME");
    
    if (!homeDir) {
        sendResponse(clientSocket, "Failed to get HOME directory");
        return;
    }

    DIR *dir = opendir(homeDir);
    
    
    if (!dir) {
        sendResponse(clientSocket, "Failed to open home directory");
        return;
    }

    struct dirent *entry;
    char *directories[MAX_DIRS];
    int numDirs = 0;

    // Read each entry in the home directory
    while ((entry = readdir(dir)) != NULL && numDirs < MAX_DIRS) {
        // Check if the entry is a directory and is not "." or ".."
        if (entry->d_type == DT_DIR && strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            char path[MAX_PATH_LEN];
            // Construct the full path of the directory entry
            snprintf(path, sizeof(path), "%s/%s", homeDir, entry->d_name);

            struct stat st;
            // Get file stats of the directory entry
            if (stat(path, &st) != 0) {
                
                fprintf(stderr, "Failed to get file stats for %s\n", path);
                continue; // Skip to the next entry
            }

            // Store the name of the directory entry in the directories array
            directories[numDirs++] = strdup(entry->d_name);
        }
    }

    
    closedir(dir);

    // Sort directories based on the specified option ("-a" for alphabetical, "-t" for creation time)
    if (strcmp(option, "-a") == 0) {
        qsort(directories, numDirs, sizeof(char *), compareNames); // Sort by name
    } else if (strcmp(option, "-t") == 0) {
        qsort(directories, numDirs, sizeof(char *), compareCreationTime); // Sort by creation time
    } else {
        
        sendResponse(clientSocket, "Invalid dirlist option");
        return;
    }

    char result[MAX_BUFFER_SIZE] = "";
    // Build the result string containing sorted directory names
    for (int i = 0; i < numDirs; i++) {
        strcat(result, directories[i]); // Append directory name to the result
        strcat(result, "\n"); // Append newline character
        free(directories[i]); // Free the allocated memory for directory name
    }

    // Send the result string containing sorted directory names to the client
    sendResponse(clientSocket, result);
}



void getFileDetails(int clientSocket, const char *filename) {
    // Construct the full file path using the user's home directory and the specified filename
    char filePath[MAX_PATH_LEN];
    snprintf(filePath, sizeof(filePath), "%s/%s", getenv("HOME"), filename);

    // Retrieve file information (stat) to check if the file exists
    struct stat fileInfo;
    if (stat(filePath, &fileInfo) == -1) {
        
        sendResponse(clientSocket, "File not found");
        return;
    }

    // Format file details into a string including name, size, permissions, and creation date
    char details[MAX_BUFFER_SIZE];
    snprintf(details, sizeof(details), "Name: %s\nSize: %lld bytes\nPermissions: %o\nDate Created: %s",
             filename, (long long)fileInfo.st_size, fileInfo.st_mode & 0777, ctime(&fileInfo.st_ctime));

    
    sendResponse(clientSocket, details);
}


void createArchive(const char *archivePath, const char *sourceDir) {
    // Initialize a new archive object for writing
    struct archive *a;
    struct archive_entry *entry;
    a = archive_write_new();
    
    // Add a gzip filter and set the archive format to pax restricted
    archive_write_add_filter_gzip(a);
    archive_write_set_format_pax_restricted(a);
    
    // Open the source directory for reading
    DIR *dir = opendir(sourceDir);
    if (dir == NULL) {
        
        fprintf(stderr, "Failed to open directory for archiving\n");
        return;
    }

    // Iterate through each entry in the directory
    struct dirent *entryDir;
    char filePath[MAX_PATH_LEN];
    while ((entryDir = readdir(dir)) != NULL) {
        // Process regular files only
        if (entryDir->d_type == DT_REG) {
            // Construct full path to the file
            snprintf(filePath, sizeof(filePath), "%s/%s", sourceDir, entryDir->d_name);
            
            // Retrieve file metadata
            struct stat st;
            stat(filePath, &st);
            
            // Create a new archive entry and populate it with file metadata
            entry = archive_entry_new();
            archive_entry_copy_stat(entry, &st);
            archive_entry_set_pathname(entry, entryDir->d_name);
            
            // Write the entry header into the archive
            archive_write_header(a, entry);
            
            
            FILE *file = fopen(filePath, "r");
            if (file == NULL) {
                fprintf(stderr, "Failed to open file for archiving\n");
                return;
            }
            
            // Read and write file contents into the archive
            char buff[8192];
            size_t len;
            while ((len = fread(buff, 1, sizeof(buff), file)) > 0) {
                archive_write_data(a, buff, len);
            }
            
            
            fclose(file);
            archive_entry_free(entry);
        }
    }

    // Close the directory and finalize the archive writing
    closedir(dir);
    archive_write_close(a);
    archive_write_free(a);
}


void sendFilesBySizeRange(int clientSocket, long long minSize, long long maxSize) {
    
    const char *homeDir = getenv("HOME");
    if (!homeDir) {
        
        sendResponse(clientSocket, "Failed to get HOME directory");
        return;
    }

   
    DIR *dir = opendir(homeDir);
    if (!dir) {
        
        sendResponse(clientSocket, "Failed to open home directory");
        return;
    }

    // Create path for the temporary archive file
    char archivePath[MAX_PATH_LEN];
    snprintf(archivePath, sizeof(archivePath), "%s/%s", homeDir, "temp.tar.gz");

    // Create an archive from the HOME directory
    createArchive(archivePath, homeDir);

    
    closedir(dir);

    // Check if the archive was successfully created
    struct stat st;
    if (stat(archivePath, &st) == -1) {
        
        sendResponse(clientSocket, "No file found");
    } else {
        // Send the path to the archive file as a response
        sendResponse(clientSocket, archivePath);
    }
}



// Create an archive containing files from the HOME directory that match specified extensions
void sendFilesByExtensions(int clientSocket, const char **extensions, int numExtensions) {
    
    const char *homeDir = getenv("HOME");
    if (!homeDir) {
        // Send an error response if HOME directory retrieval failed
        sendResponse(clientSocket, "Failed to get HOME directory");
        return;
    }

    
    DIR *dir = opendir(homeDir);
    if (!dir) {
        
        sendResponse(clientSocket, "Failed to open home directory");
        return;
    }

    // Create a new libarchive write structure
    struct archive *a = archive_write_new();
    if (!a) {
        
        sendResponse(clientSocket, "Failed to create archive");
        closedir(dir);
        return;
    }

    // Add compression and set archive format
    archive_write_add_filter_gzip(a);
    archive_write_set_format_pax_restricted(a);

    // Create path for the temporary archive file
    char archivePath[MAX_PATH_LEN];
    snprintf(archivePath, sizeof(archivePath), "%s/%s", homeDir, "temp.tar.gz");

    
    if (archive_write_open_filename(a, archivePath) != ARCHIVE_OK) {
        // Send an error response if opening the archive file failed
        sendResponse(clientSocket, "Failed to open archive for writing");
        archive_write_free(a);
        closedir(dir);
        return;
    }

    // Traverse files in the HOME directory
    struct dirent *entry;
    int filesFound = 0;

    while ((entry = readdir(dir)) != NULL) {
        // Check if the entry represents a regular file
        if (entry->d_type == DT_REG) {
            // Extract file extension
            char *extension = strrchr(entry->d_name, '.');
            if (extension != NULL) {
                extension++; // Move past the dot

                // Check if the file extension matches any specified extensions
                for (int i = 0; i < numExtensions; i++) {
                    if (strcmp(extension, extensions[i]) == 0) {
                        // Create full file path
                        char filePath[MAX_PATH_LEN];
                        snprintf(filePath, sizeof(filePath), "%s/%s", homeDir, entry->d_name);

                        // Get file stats
                        struct stat st;
                        if (stat(filePath, &st) == -1) {
                            // Skip if failed to get file stats
                            fprintf(stderr, "Failed to get file stats: %s\n", strerror(errno));
                            continue;
                        }

                        // Create a new archive entry
                        struct archive_entry *entry_archive = archive_entry_new();
                        if (!entry_archive) {
                            fprintf(stderr, "Failed to create archive entry\n");
                            continue;
                        }

                        // Copy file stats to archive entry
                        archive_entry_copy_stat(entry_archive, &st);
                        archive_entry_set_pathname(entry_archive, entry->d_name);

                        // Write archive entry header
                        if (archive_write_header(a, entry_archive) != ARCHIVE_OK) {
                            fprintf(stderr, "Failed to write header to archive: %s\n", archive_error_string(a));
                            archive_entry_free(entry_archive);
                            continue;
                        }

                        
                        FILE *file = fopen(filePath, "r");
                        if (!file) {
                            // Skip if failed to open file for archiving
                            fprintf(stderr, "Failed to open file for archiving: %s\n", strerror(errno));
                            archive_entry_free(entry_archive);
                            continue;
                        }

                        // Read and write file data to archive
                        char buff[8192];
                        size_t len;
                        while ((len = fread(buff, 1, sizeof(buff), file)) > 0) {
                            if (archive_write_data(a, buff, len) != len) {
                                fprintf(stderr, "Failed to write file data to archive: %s\n", archive_error_string(a));
                                fclose(file);
                                archive_entry_free(entry_archive);
                                break;
                            }
                        }

                        
                        fclose(file);
                        archive_entry_free(entry_archive);
                        filesFound++;
                    }
                }
            }
        }
    }

    
    closedir(dir);

    // Finalize the archive and free resources
    archive_write_close(a);
    archive_write_free(a);

    // Send response based on files found
    if (filesFound > 0) {
        // Send path to the created archive if files were archived
        sendResponse(clientSocket, archivePath);
    } else {
        // Send message if no files matching specified extensions were found
        sendResponse(clientSocket, "No files found matching specified extensions");
    }
}

void createArchiveFilteredByDate(const char *archivePath, const char *sourceDir, time_t targetDate, int beforeOrEqual) {
    struct archive *a;
    struct archive_entry *entry;

    // Create a new libarchive write structure
    a = archive_write_new();
    archive_write_add_filter_gzip(a);
    archive_write_set_format_pax_restricted(a);

    // Open the archive file for writing
    archive_write_open_filename(a, archivePath);

    
    DIR *dir = opendir(sourceDir);
    if (!dir) {
        // Print an error message and return if opening the directory fails
        fprintf(stderr, "Failed to open directory for archiving\n");
        return;
    }

    struct dirent *entryDir;
    char filePath[MAX_PATH_LEN];

    // Traverse files in the source directory
    while ((entryDir = readdir(dir)) != NULL) {
        // Process regular files
        if (entryDir->d_type == DT_REG) {
            // Construct full file path
            snprintf(filePath, sizeof(filePath), "%s/%s", sourceDir, entryDir->d_name);
            struct stat st;
            stat(filePath, &st);
            time_t fileCreationTime = st.st_ctime;

            // Check if the file creation time meets the specified condition
            if ((beforeOrEqual && fileCreationTime <= targetDate) ||
                (!beforeOrEqual && fileCreationTime >= targetDate)) {
                // Create a new archive entry and set its attributes
                entry = archive_entry_new();
                archive_entry_copy_stat(entry, &st);
                archive_entry_set_pathname(entry, entryDir->d_name);
                archive_write_header(a, entry);

                // Open the file for reading
                FILE *file = fopen(filePath, "r");
                if (file == NULL) {
                    // Print an error message and skip to the next file if opening the file fails
                    fprintf(stderr, "Failed to open file for archiving\n");
                    continue;
                }

                // Read and write file data to the archive
                char buff[8192];
                size_t len;
                while ((len = fread(buff, 1, sizeof(buff), file)) > 0) {
                    archive_write_data(a, buff, len);
                }

                // Close the file and free the archive entry
                fclose(file);
                archive_entry_free(entry);
            }
        }
    }

    // Close the source directory and finalize the archive
    closedir(dir);
    archive_write_close(a);
    archive_write_free(a);
}



// Function to create an archive containing files modified before a specified date
// - date: Date string in the format "YYYY-MM-DD" to specify the target date
void sendFilesByDateBefore(int clientSocket, const char *date) {
    const char *homeDir = getenv("HOME");
    if (!homeDir) {
        sendResponse(clientSocket, "Failed to get HOME directory");
        return;
    }

    // Convert date string to time_t
    struct tm tm = {0};
    strptime(date, "%Y-%m-%d", &tm);
    time_t targetDate = mktime(&tm);

    // Create an archive containing files modified before the target date
    char archivePath[MAX_PATH_LEN];
    snprintf(archivePath, sizeof(archivePath), "%s/%s", homeDir, "temp.tar.gz");
    createArchiveFilteredByDate(archivePath, homeDir, targetDate, 1);

    // Check if the archive was created successfully
    struct stat st;
    if (stat(archivePath, &st) == -1) {
        sendResponse(clientSocket, "No files found");
    } else {
        sendResponse(clientSocket, archivePath);
    }
}

// Function to create an archive containing files modified after a specified date
// - date: Date string in the format "YYYY-MM-DD" to specify the target date
void sendFilesByDateAfter(int clientSocket, const char *date) {
    const char *homeDir = getenv("HOME");
    if (!homeDir) {
        sendResponse(clientSocket, "Failed to get HOME directory");
        return;
    }

    // Convert date string to time_t
    struct tm tm = {0};
    strptime(date, "%Y-%m-%d", &tm);
    time_t targetDate = mktime(&tm);

    // Create an archive containing files modified after the target date
    char archivePath[MAX_PATH_LEN];
    snprintf(archivePath, sizeof(archivePath), "%s/%s", homeDir, "temp.tar.gz");
    createArchiveFilteredByDate(archivePath, homeDir, targetDate, 0);

    // Check if the archive was created successfully
    struct stat st;
    if (stat(archivePath, &st) == -1) {
        sendResponse(clientSocket, "No files found");
    } else {
        sendResponse(clientSocket, archivePath);
    }
}


//Handling all clients options which are provided by clients.
void handleClient(int clientSocket) {
    char buffer[1024];
    int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    if (bytesReceived < 0) {
        perror("Error receiving data from client");
        close(clientSocket);
        exit(EXIT_FAILURE);
    } else if (bytesReceived == 0) {
        printf("Client disconnected\n");
        close(clientSocket);
        exit(EXIT_SUCCESS);
    }

    buffer[bytesReceived] = '\0'; // Null-terminate the received data

    // Parse and process command
    char *command = strtok(buffer, " \n"); // Tokenize by space or newline
    if (command == NULL) {
        sendResponse(clientSocket, "Invalid command\n");
        close(clientSocket);
        exit(EXIT_SUCCESS);
    }

    if (strcmp(command, "dirlist") == 0) {
        char *option = strtok(NULL, " \n");
        if (option != NULL && (strcmp(option, "-a") == 0 || strcmp(option, "-t") == 0)) {
            listDirectories(clientSocket, option);
        } else {
            sendResponse(clientSocket, "Invalid dirlist command syntax\n");
        }
    } else if (strcmp(command, "w24fn") == 0) {
        char *filename = strtok(NULL, " \n");
        if (filename != NULL) {
            getFileDetails(clientSocket, filename);
        } else {
            sendResponse(clientSocket, "Invalid w24fn command syntax\n");
        }
    } else if (strcmp(command, "w24fz") == 0) {
        char *minSizeStr = strtok(NULL, " \n");
        char *maxSizeStr = strtok(NULL, " \n");
        if (minSizeStr != NULL && maxSizeStr != NULL) {
            long long minSize = atoll(minSizeStr);
            long long maxSize = atoll(maxSizeStr);
            sendFilesBySizeRange(clientSocket, minSize, maxSize);
        } else {
            sendResponse(clientSocket, "Invalid w24fz command syntax\n");
        }
    } else if (strcmp(command, "w24ft") == 0) {
        const char *extensions[3];
        int i = 0;
        char *extension = strtok(NULL, " \n");
        while (extension != NULL && i < 3) {
            extensions[i++] = extension;
            extension = strtok(NULL, " \n");
        }
        if (i > 0) {
            sendFilesByExtensions(clientSocket, extensions, i);
        } else {
            sendResponse(clientSocket, "Invalid w24ft command syntax\n");
        }
    } else if (strcmp(command, "w24fdb") == 0) {
        char *date = strtok(NULL, " \n");
        if (date != NULL) {
            sendFilesByDateBefore(clientSocket, date);
        } else {
            sendResponse(clientSocket, "Invalid w24fdb command syntax\n");
        }
    } else if (strcmp(command, "w24fda") == 0) {
        char *date = strtok(NULL, " \n");
        if (date != NULL) {
            sendFilesByDateAfter(clientSocket, date);
        } else {
            sendResponse(clientSocket, "Invalid w24fda command syntax\n");
        }
    } else if (strcmp(command, "quitc") == 0) {
        sendResponse(clientSocket, "Connection closed by client\n");
    } else {
        sendResponse(clientSocket, "Invalid command\n");
    }

    close(clientSocket);
    exit(EXIT_SUCCESS);
}



int main() {
    int serverSocket, clientSocket;
    struct sockaddr_in serverAddr, clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);
    pid_t childPids[MAX_CLIENTS] = {0};
    int numClients = 0;

    // Create server socket
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Setup server address structure
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(MIRROR2_PORT);

    // Bind the socket to the server address
    if (bind(serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("Socket bind failed");
        close(serverSocket);
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(serverSocket, SOMAXCONN) < 0) {
        perror("Socket listen failed");
        close(serverSocket);
        exit(EXIT_FAILURE);
    }

    printf("Mirror2 Server is listening on port %d\n", MIRROR2_PORT);

    // Accept and handle client connections
    while (1) {
        clientSocket = accept(serverSocket, (struct sockaddr *)&clientAddr, &clientAddrLen);
        if (clientSocket < 0) {
            perror("Socket accept failed");
            continue;
        }

        printf("Accepted new client connection\n");

        // Check if maximum number of clients is reached
        if (numClients >= MAX_CLIENTS) {
            send(clientSocket, "Mirror1 Server is busy. Try again later.\n", 40, 0);
            close(clientSocket);
            continue;
        }

        // Fork a child process to handle the client
        pid_t pid = fork();
        if (pid == -1) {
            perror("Fork failed");
            close(clientSocket);
        } else if (pid == 0) {
            // Child process
            close(serverSocket); // Close server socket in child
            handleClient(clientSocket); // Handle client request
        } else {
            // Parent process
            childPids[numClients++] = pid;
            close(clientSocket); // Close client socket in parent
        }

        // Clean up terminated child processes
        for (int i = 0; i < numClients; i++) {
            pid_t terminatedPid = waitpid(childPids[i], NULL, WNOHANG);
            if (terminatedPid > 0) {
                // Child process terminated, remove from the list
                childPids[i] = 0;
                numClients--;
            }
        }
    }

    close(serverSocket);

    return 0;
}

