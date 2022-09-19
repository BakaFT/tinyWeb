#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h> // open file
#include <sys/socket.h>
#include <sys/stat.h> // file info
#include <sys/mman.h> // file mapping
#include <netinet/in.h>
#include "mime.h" // simple k-v dictionary

#define BUFFERSIZE 1024
/*
 * for all the infos about BSD socket: https://en.wikipedia.org/wiki/Berkeley_sockets#socket
 */

/*
 * wrapper for perror used for bad syscalls
 */
void error(char *msg)
{
  perror(msg);
  exit(1);
}

/*
 * returns an error message to the client
 */
void error_c(FILE *stream, char *cause, char *status_code,
             char *shortmsg, char *longmsg, char *servername)
{
  fprintf(stream, "HTTP/1.1 %s %s\n", status_code, shortmsg);
  fprintf(stream, "Content-type: text/html\n");
  fprintf(stream, "\n");
  fprintf(stream, "<html><title>Server Error</title>");
  fprintf(stream, "<body bgcolor="
                  "ffffff"
                  ">\n");
  fprintf(stream, "<h1>%s %s</h1>\n", status_code, shortmsg);
  fprintf(stream, "<p>%s: %s\n", longmsg, cause);
  fprintf(stream, "<hr><em>%s</em>\n", servername);
}

int main(int argc, char **argv)
{

  // variables for connection management
  int parentfd;                       // parent socket
  int childfd;                        // child socket
  int port;                           // port to listen on
  int optval;                         // flag value for setsockopt
  struct sockaddr_in serveraddr;      // server addr
  struct sockaddr_in clientaddr;      // client addr
  int clientlen = sizeof(clientaddr); // size of client's address

  // variables for connection I/O
  FILE *stream;              // stream of childfd
  char buf[BUFFERSIZE];      // message buffer
  char method[BUFFERSIZE];   // request method
  char uri[BUFFERSIZE];      // request uri
  char version[BUFFERSIZE];  // request http version
  char filename[BUFFERSIZE]; // path derived from uri
  char filetype[BUFFERSIZE]; // path derived from uri
  char *p;                   // temporary pointer
  int is_static;             // flag value for static request
  struct stat fileinfo;      // file status struct
  int fd;                    // static content file descriptor

  char *servername = "BakaFT's C-Written Server";

  // commandline arg check
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  port = atoi(argv[1]);

  // open socket descriptor
  parentfd = socket(AF_INET, SOCK_STREAM, 0);
  //The function returns -1 if an error occurred. Otherwise, it returns an integer representing the newly assigned descriptor.
  if (parentfd < 0)
    error("ERROR opening socket");

  /* 
    SO_REUSEADDR allows your server to FORCIBLY bind to an address which is in a TIME_WAIT state after closed.

    Reference:
    https://docs.microsoft.com/en-us/windows/win32/winsock/using-so-reuseaddr-and-so-exclusiveaddruse
    https://stackoverflow.com/questions/3229860/what-is-the-meaning-of-so-reuseaddr-setsockopt-option-linux

  */
  optval = 1;
  setsockopt(parentfd, SOL_SOCKET, SO_REUSEADDR,
             (const void *)&optval, sizeof(int));

  // bind an address to socket
  bzero((char *)&serveraddr, sizeof(serveraddr));
  serveraddr.sin_family = AF_INET;                // IPv4 protocol
  serveraddr.sin_addr.s_addr = htonl(INADDR_ANY); // listen on all available interfaces.
  serveraddr.sin_port = htons((unsigned short)port);
  if (bind(parentfd, (struct sockaddr *)&serveraddr,
           sizeof(serveraddr)) < 0)
    error("ERROR on binding");

  // get ready to accept connection requests
  if (listen(parentfd, 10) < 0) // allow up to 10 requests to queue up
    error("ERROR on listen");

  /* 
   * main loop: wait for a connection request, parse HTTP,
   * serve requested content, close connection.
   */

  while (1)
  {

    // wait for a connection request
    childfd = accept(parentfd, (struct sockaddr *)&clientaddr, &clientlen);
    /* 
      accept() returns the new socket descriptor for the accepted connection, or the value -1 if an error occurs.
      Why cast sockaddr_in* to sockaddr* here?
      https://stackoverflow.com/questions/21099041/why-do-we-cast-sockaddr-in-to-sockaddr-when-calling-bind
     */

    if (childfd < 0)
    {
      error("ERROR on accept");
      continue;
    }

    /* open the child socket descriptor as a stream */
    if ((stream = fdopen(childfd, "r+")) == NULL)
      error("ERROR on fdopen");

    /* get the HTTP request line */
    fgets(buf, BUFFERSIZE, stream);
    printf("HTTP Request body:\n%s", buf);
    sscanf(buf, "%s %s %s\n", method, uri, version);

    /*  only supports the GET method */
    if (strcasecmp(method, "GET"))
    /*
     strcasecmp() return an integer greater than, equal
     to, or less than 0, according as s1 is lexicographically greater than,
     equal to, or less than s2 
    */
    {
      error_c(stream, method, "501", "Not Implemented",
              "Server does not support this method", servername);
      fclose(stream);
      close(childfd);
      continue;
    }

    /* read and ignore the HTTP headers */
    fgets(buf, BUFFERSIZE, stream);
    printf("%s", buf);
    while (strcmp(buf, "\r\n"))
    {
      fgets(buf, BUFFERSIZE, stream);
      printf("%s", buf);
    }

    // parse the uri
    if (!strstr(uri, "?"))
    { //static content
      is_static = 1;
      strcpy(filename, ".");
      strcat(filename, uri);
      if (strlen(uri) == 1) // namely "/"
      {
        strcat(filename, "index.html"); // if uri is "/" then add "index.html" by default
      }
      else
      {
        //do nothing
      }
    }
    else
    { /* dynamic content 
          not supported */
      error_c(stream, uri, "501", "Not Implemented",
              "Server does not support dynamic content", servername);
      fclose(stream);
      close(childfd);
      continue;
    }

    /* make sure the file exists */
    /* stat()
     Upon successful completion a value of 0 is returned.  Otherwise, a value
     of -1 is returned and errno is set to indicate the error.
    */
    if (stat(filename, &fileinfo) < 0) // and here we store file info to fileinfo
    {
      error_c(stream, filename, "404", "Not found",
              "Server couldn't find this file", servername);
      fclose(stream);
      close(childfd);
      continue;
    }

    /* Use mmap to return arbitrary-sized response body 
          open file and map the contents to memory, then copy it from memory to stream and send out thourgh socket.
          use munmap() to free the mapped memory at last.
      */

    fd = open(filename, O_RDONLY);
    p = mmap(0, fileinfo.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

    if (is_static)
    {
      // Here we assume that only 1 extension in filename
      // which means there will not be filename like “tom.html.jpg"
      strcpy(filetype, "text/plain");
      for (int i = 0; i < ARRAYSIZE; i++)
      {
        if (strstr(filename, mimelist[i].key))
        {
          strcpy(filetype, mimelist[i].value);
          break;
        }
      }

      // print response header
      fprintf(stream, "HTTP/1.1 200 OK\n");
      fprintf(stream, "Allow: GET\n");
      fprintf(stream, "Server: %s\n", servername);
      fprintf(stream, "Content-length: %d\n", (int)fileinfo.st_size); // file size in bytes
      fprintf(stream, "Content-type: %s;charset=UTF-8\n", filetype);
      fprintf(stream, "\r\n");
      fflush(stream);

      fwrite(p, 1, fileinfo.st_size, stream);
      munmap(p, fileinfo.st_size); // undo mmap
    }

    // close this socket and wait for the next accept()
    fclose(stream);
    close(childfd);
  }
}