/*
 * http-server.c
 */

#include <stdio.h>      /* for printf() and fprintf() */
#include <sys/socket.h> /* for socket(), bind(), and connect() */
#include <arpa/inet.h>  /* for sockaddr_in and inet_ntoa() */
#include <stdlib.h>     /* for atoi() and exit() */
#include <string.h>     /* for memset() */
#include <unistd.h>     /* for close() */
#include <time.h>       /* for time() */
#include <netdb.h>      /* for gethostbyname() */
#include <signal.h>     /* for signal() */
#include <sys/stat.h>   /* for stat() */

#define MAX_CONNECTIONS 5   /* Maximum outstanding connection requests */

#define BUFFER_SIZE 4096

static void die(const char *msg)
{
    perror(msg);
    exit(1); 
}

/*
 * Set up a socket to listen on the given port.
 */
static int setUpServerSocket(unsigned short port)
{
    int serverSocket;
    struct sockaddr_in serverAddress;

    /* Create a socket for incoming connections */
    if ((serverSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
        terminate("socket() failed");

    /* Prepare the server address structure */
    memset(&serverAddress, 0, sizeof(serverAddress));    /* Zero out structure */
    serverAddress.sin_family = AF_INET;                   /* Internet address family */
    serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);    /* Any incoming interface */
    serverAddress.sin_port = htons(port);                 /* Local port */

    /* Bind to the local address */
    if (bind(serverSocket, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) < 0)
        terminate("bind() failed");

    /* Mark the socket so it will listen for incoming connections */
    if (listen(serverSocket, MAX_CONNECTIONS) < 0)
        terminate("listen() failed");

    return serverSocket;
}

/*
 * Establish a persistent connection to the mdb-lookup-server
 * running on mdbHost at mdbPort.
 */
static int establishMdbConnection(const char *mdbHost, unsigned short mdbPort)
{
    int connectionSocket;
    struct sockaddr_in serverAddress;
    struct hostent *hostEntry;
    
    // Resolve server IP from host name
    if ((hostEntry = gethostbyname(mdbHost)) == NULL) {
        terminate("gethostbyname failed");
    }
    char *serverIP = inet_ntoa(*(struct in_addr *)hostEntry->h_addr);
    
    // Create the socket
    if ((connectionSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        terminate("socket failed");
    }

    // Set up the server address structure
    memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = inet_addr(serverIP);
    serverAddress.sin_port = htons(mdbPort);

    // Connect to the server
    if (connect(connectionSocket, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) < 0) {
        terminate("connect failed");
    }

    return connectionSocket;
}

/*
 * A wrapper for send() with error checking and logging.
 * Returns -1 on failure.
 * 
 * This function assumes that buf is a null-terminated string, so
 * do not use this function to send binary data.
 */
ssize_t transmit(int sock, const char *buffer)
{
    size_t len = strlen(buffer);
    ssize_t result = send(sock, buffer, len, 0);
    if (result != len) {
        perror("send() failed");
        return -1;
    }
    else 
        return result;
}

/*
 * HTTP/1.0 status codes and their corresponding reason phrases.
 */

static struct {
    int code;
    char *message;
} HttpStatusCodes[] = {
    { 200, "OK" },
    { 201, "Created" },
    { 202, "Accepted" },
    { 204, "No Content" },
    { 301, "Moved Permanently" },
    { 302, "Moved Temporarily" },
    { 304, "Not Modified" },
    { 400, "Bad Request" },
    { 401, "Unauthorized" },
    { 403, "Forbidden" },
    { 404, "Not Found" },
    { 500, "Internal Server Error" },
    { 501, "Not Implemented" },
    { 502, "Bad Gateway" },
    { 503, "Service Unavailable" },
    { 0, NULL } // marks the end of the list
};

static inline const char *getStatusMessage(int statusCode)
{
    int index = 0;
    while (HttpStatusCodes[index].code > 0) {
        if (HttpStatusCodes[index].code == statusCode)
            return HttpStatusCodes[index].message;
        index++;
    }
    return "Unknown Status Code";
}


/*
 * Send an HTTP status line followed by a blank line.
 */
static void sendHttpStatus(int clientSocket, int statusCode)
{
    char buffer[1000];
    const char *statusMessage = getStatusMessage(statusCode);

    // Create the status line in the buffer
    sprintf(buffer, "HTTP/1.0 %d ", statusCode);
    strcat(buffer, statusMessage);
    strcat(buffer, "\r\n");

    // Send a blank line to indicate the end of headers
    strcat(buffer, "\r\n");

    // If the status is not 200, send the status message in HTML format
    if (statusCode != 200) {
        char body[1000];
        sprintf(body, 
                "<html><body>\n"
                "<h1>%d %s</h1>\n"
                "</body></html>\n",
                statusCode, statusMessage);
        strcat(buffer, body);
    }

    // Transmit the buffer to the client
    transmit(clientSocket, buffer);
}


/*
 * Handle /mdb-lookup or /mdb-lookup?key=... requests.
 * Returns the HTTP status code that was sent to the browser.
 */
static int processMdbRequest(const char *requestURI, FILE *mdbFile, int mdbSocket, int clientSocket)
{
    // Send status line
    int statusCode = 200;
    sendHttpStatus(clientSocket, statusCode);

    // Send the HTML form
    const char *form = 
        "<html><body>\n"
        "<h1>mdb-lookup</h1>\n"
        "<p>\n"
        "<form method=GET action=/mdb-lookup>\n"
        "lookup: <input type=text name=key>\n"
        "<input type=submit>\n"
        "</form>\n"
        "<p>\n";
    if (transmit(clientSocket, form) < 0) goto cleanup;

    // If requestURI begins with /mdb-lookup?key=, perform the lookup
    const char *keyURI = "/mdb-lookup?key=";

    if (strncmp(requestURI, keyURI, strlen(keyURI)) == 0) {

        // Send the lookup key to the mdb-lookup-server
        const char *key = requestURI + strlen(keyURI);
        fprintf(stderr, "looking up [%s]: ", key);
        if (transmit(mdbSocket, key) < 0) goto cleanup;
        if (transmit(mdbSocket, "\n") < 0) goto cleanup;

        // Read lines from the mdb-lookup-server and send them to the browser in HTML table format
        char line[1000];
        const char *tableStart = "<p><table border>";
        if (transmit(clientSocket, tableStart) < 0) goto cleanup;

        int rowIndex = 1;
        for (;;) {
            // Read a line from the mdb-lookup-server
            if (fgets(line, sizeof(line), mdbFile) == NULL) {
                if (ferror(mdbFile))
                    perror("\nmdb-lookup-server connection failed");
                else 
                    fprintf(stderr,"\nmdb-lookup-server connection terminated");
                goto cleanup;
            }
            // If blank line, end of results
            if (strcmp("\n", line) == 0) {
                break;
            }
            // Format the line as a table row
            const char *tableRow = (rowIndex++ % 2) ? "\n<tr><td>" : "\n<tr><td bgcolor=yellow>";
            if (transmit(clientSocket, tableRow) < 0) goto cleanup;
            if (transmit(clientSocket, line) < 0) goto cleanup;
        }

        const char *tableEnd = "\n</table>\n";
        if (transmit(clientSocket, tableEnd) < 0) goto cleanup;
    }

    // Close the HTML page
    if (transmit(clientSocket, "</body></html>\n") < 0) goto cleanup;

cleanup:
    return statusCode;
}

/*
 * Handle static file requests.
 * Returns the HTTP status code that was sent to the browser.
 */
static int processFileRequest(const char *webRoot, const char *requestURI, int clientSocket)
{
    int statusCode;
    FILE *file = NULL;

    // Construct the file path from webRoot and requestURI
    char *filePath = (char *)malloc(strlen(webRoot) + strlen(requestURI) + 100);
    if (filePath == NULL)
        terminate("malloc failed");
    strcpy(filePath, webRoot);
    strcat(filePath, requestURI);
    if (filePath[strlen(filePath)-1] == '/') {
        strcat(filePath, "index.html");
    }

    // Check if the requested file is a directory
    struct stat fileStat;
    if (stat(filePath, &fileStat) == 0 && S_ISDIR(fileStat.st_mode)) {
        statusCode = 403; // "Forbidden"
        sendHttpStatus(clientSocket, statusCode);
        goto cleanup;
    }

    // If unable to open the file, send "404 Not Found"
    file = fopen(filePath, "rb");
    if (file == NULL) {
        statusCode = 404; // "Not Found"
        sendHttpStatus(clientSocket, statusCode);
        goto cleanup;
    }

    // Otherwise, send "200 OK" followed by the file content
    statusCode = 200; // "OK"
    sendHttpStatus(clientSocket, statusCode);

    // Send the file contents
    size_t bytesRead;
    char buffer[BUFFER_SIZE];
    while ((bytesRead = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (send(clientSocket, buffer, bytesRead, 0) != bytesRead) {
            perror("\nsend() failed");
            break;
        }
    }

    // Check for errors during file read
    if (ferror(file))
        perror("fread failed");

cleanup:
    free(filePath);
    if (file)
        fclose(file);

    return statusCode;
}

int main(int argc, char *argv[])
{
    // Ignore SIGPIPE so that we don't terminate when we call send() on a disconnected socket.
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
        terminate("signal() failed");

    if (argc != 5) {
        fprintf(stderr, "usage: %s <server_port> <web_root> <mdb-lookup-host> <mdb-lookup-port>\n", argv[0]);
        exit(1);
    }

    unsigned short port = atoi(argv[1]);
    const char *rootDirectory = argv[2];
    const char *mdbHost = argv[3];
    unsigned short mdbPort = atoi(argv[4]);

    int mdbSocket = establishMdbConnection(mdbHost, mdbPort);
    FILE *mdbFile = fdopen(mdbSocket, "r");
    if (mdbFile == NULL)
        terminate("fdopen failed");

    int serverSocket = setUpServerSocket(port);

    char line[1000];
    char request[1000];
    int statusCode;
    struct sockaddr_in clientAddress;

    for (;;) {
        /*
         * Wait for a client to connect.
         */

        unsigned int clientLength = sizeof(clientAddress); 
        int clientSocket = accept(serverSocket, (struct sockaddr *)&clientAddress, &clientLength);
        if (clientSocket < 0)
            terminate("accept() failed");

        FILE *clientFile = fdopen(clientSocket, "r");
        if (clientFile == NULL)
            terminate("fdopen failed");

        /*
         * Parse the request line.
         */

        char *httpMethod = "";
        char *uri = "";
        char *version = "";

        if (fgets(request, sizeof(request), clientFile) == NULL) {
            statusCode = 400; // "Bad Request"
            goto loop_end;
        }

        char *delimiters = "\t \r\n"; // tab, space, new line
        httpMethod = strtok(request, delimiters);
        uri = strtok(NULL, delimiters);
        version = strtok(NULL, delimiters);
        char *extraTokens = strtok(NULL, delimiters);

        // Ensure that the request has 3 parts
        if (!httpMethod || !uri || !version || extraTokens) {
            statusCode = 501; // "Not Implemented"
            sendHttpStatus(clientSocket, statusCode);
            goto loop_end;
        }

        // We only support GET method 
        if (strcmp(httpMethod, "GET") != 0) {
            statusCode = 501; // "Not Implemented"
            sendHttpStatus(clientSocket, statusCode);
            goto loop_end;
        }

        // Only support HTTP/1.0 and HTTP/1.1
        if (strcmp(version, "HTTP/1.0") != 0 && 
            strcmp(version, "HTTP/1.1") != 0) {
            statusCode = 501; // "Not Implemented"
            sendHttpStatus(clientSocket, statusCode);
            goto loop_end;
        }
        
        // Ensure requestURI starts with "/"
        if (!uri || *uri != '/') {
            statusCode = 400; // "Bad Request"
            sendHttpStatus(clientSocket, statusCode);
            goto loop_end;
        }

        // Ensure no directory traversal
        int len = strlen(uri);
        if (len >= 3) {
            char *tail = uri + (len - 3);
            if (strcmp(tail, "/..") == 0 || 
                    strstr(uri, "/../") != NULL)
            {
                statusCode = 400; // "Bad Request"
                sendHttpStatus(clientSocket, statusCode);
                goto loop_end;
            }
        }

        // Skip headers
        while (1) {
            if (fgets(line, sizeof(line), clientFile) == NULL) {
                statusCode = 400; // "Bad Request"
                goto loop_end;
            }
            if (strcmp("\r\n", line) == 0 || strcmp("\n", line) == 0) {
                break;
            }
        }

        // Now handle the request
        const char *mdbRequestURI = "/mdb-lookup";
        if (strncmp(uri, mdbRequestURI, strlen(mdbRequestURI)) == 0) {
            statusCode = processMdbRequest(uri, mdbFile, mdbSocket, clientSocket);
        } else {
            statusCode = processFileRequest(rootDirectory, uri, clientSocket);
        }

loop_end:
        fclose(clientFile);
        close(clientSocket);
    }

    return 0;
}
