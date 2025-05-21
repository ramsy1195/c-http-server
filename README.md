# C HTTP Server 

This project implements a basic HTTP/1.0-compatible server in C that supports both static file serving and dynamic lookup requests. It integrates with an `mdb-lookup-server` backend over a TCP socket, returning formatted HTML responses to client browsers.

## Features
- Serves static files from a specified `webroot/` directory
- Parses and validates HTTP GET requests
- Securely handles request URIs (blocks `/../` directory traversal)
- Supports `mdb-lookup?key=...` query via persistent TCP connection to a backend server
- Dynamic HTML output for query results
- Sends appropriate HTTP status codes and reason phrases
- Minimal external dependencies

## Directory Structure
```text
.
├── http-server.c         # Main C source file (HTTP server logic)
├── Makefile              # Build configuration
├── README.md             # Project documentation
├── webroot/              # Static files (HTML, CSS, etc.)
│   └── index.html        # Default page served at '/'
```
## Build Instructions
To compile the project:
```bash
make
```
To clean up build artifacts:
```bash
make clean
```

## Run Instructions
```bash
./http-server <server_port> <web_root> <mdb-lookup-host> <mdb-lookup-port>
```
- `server_port`: Port number to listen for incoming HTTP requests
- `web_root`: Path to the root directory for static file serving (e.g. webroot/)
- `mdb-lookup-host`: Hostname or IP where mdb-lookup-server is running
- `mdb-lookup-port`: Port on which mdb-lookup-server is listening

Example:
```bash
./http-server 8080 ./webroot localhost 9999
```
Make sure the `mdb-lookup-server` is running on port 9999.

Then visit in a browser:
```bash
http://localhost:8080/
http://localhost:8080/mdb-lookup
```

## Notes
- The server only supports the HTTP GET method and adheres to HTTP/1.0 and HTTP/1.1 protocol versions.
- Basic security checks are included to prevent directory traversal attacks (e.g., filtering out /../ in URLs).
- Errors are handled gracefully with proper HTTP status codes and minimal HTML response content.
- The server ignores SIGPIPE to avoid crashing on disconnected sockets during send operations.
- Communication with the mdb-lookup-server is handled over a persistent TCP connection using line-based messaging.
- All HTML is generated directly in the C code—this keeps the server simple but limits flexibility and scalability.
- Ways to improve or extend the project:
  - Support additional HTTP methods like POST or HEAD.
  - Add MIME type detection and appropriate Content-Type headers.
  - Introduce multithreading or asynchronous I/O for concurrent connections.
  - Implement HTTPS using OpenSSL.
  - Serve compressed files (e.g., gzip) or support HTTP/2 for performance.

