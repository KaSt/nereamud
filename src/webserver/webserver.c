//*****************************************************************************
//
// webserver.c
//
// This is a module that opens an HTTP server on another port that allows 
// people to request various sorts of information. Useful for displaying online
// who lists, and that sort of junk.
//
//*****************************************************************************

#include "../mud.h"
#include "../utils.h"
#include "../inform.h"
#include "../event.h"

#include "webserver.h"



//*****************************************************************************
// local variables, datastructures, functions, and defines
//*****************************************************************************

// the socket for our server
int web_control;

// our list of descriptors connected
LIST *web_descs = NULL;

// what is the mapping from query:function ?
HASHTABLE *query_table = NULL;

// the datstructure for a web descriptor
typedef struct web_socket {
  int                 control;
  struct sockaddr_in     addr;
  char   inbuf[MAX_INPUT_LEN];
  int                 buf_len;
} WEB_SOCKET;

WEB_SOCKET *newWebSocket() {
  WEB_SOCKET *sock = calloc(1, sizeof(WEB_SOCKET));
  return sock;
}

void deleteWebSocket(WEB_SOCKET *sock) {
  free(sock);
}

void webSocketClose(WEB_SOCKET *sock) {
  close(sock->control);
}

//
// convert all of the color codes and ascii characters to their
// HTML equivilants.
void bufferASCIIHTML(BUFFER *buf) {
  bufferReplace(buf, "\r", "",         TRUE);
  bufferReplace(buf, "\n", "<br>",     TRUE);
  bufferReplace(buf, "  ", " &nbsp;",  TRUE);
  bufferReplace(buf, "{n", "<font color=\"green\">",   TRUE);
  bufferReplace(buf, "{g", "<font color=\"green\">",   TRUE);
  bufferReplace(buf, "{w", "<font color=\"silver\">",  TRUE);
  bufferReplace(buf, "{p", "<font color=\"purple\">",  TRUE);
  bufferReplace(buf, "{b", "<font color=\"navy\">",    TRUE);
  bufferReplace(buf, "{y", "<font color=\"olive\">",   TRUE);
  bufferReplace(buf, "{r", "<font color=\"maroon\">",  TRUE);
  bufferReplace(buf, "{c", "<font color=\"teal\">",    TRUE);
  bufferReplace(buf, "{d", "<font color=\"black\">",   TRUE);
  bufferReplace(buf, "{G", "<font color=\"lime\">",    TRUE);
  bufferReplace(buf, "{W", "<font color=\"white\">",   TRUE);
  bufferReplace(buf, "{P", "<font color=\"magenta\">", TRUE);
  bufferReplace(buf, "{B", "<font color=\"blue\">",    TRUE);
  bufferReplace(buf, "{Y", "<font color=\"yellow\">",  TRUE);
  bufferReplace(buf, "{R", "<font color=\"red\">",     TRUE);
  bufferReplace(buf, "{C", "<font color=\"aqua\">",    TRUE);
  bufferReplace(buf, "{D", "<font color=\"grey\">",    TRUE);
}


//
// used by webSocketHandle. Replaces one char with another in the buf. 
// Returns how many replacements were made
int replace_char(char *buf, char from, char to) {
  int replacements = 0;
  for(;*buf != '\0'; buf++) {
    if(*buf == from) {
      *buf = to;
      replacements++;
    }
  }
  return replacements;
}

/*  Write a line to a socket  */

ssize_t Writeline(int sockd, const void *vptr, size_t n) {
    size_t      nleft;
    ssize_t     nwritten;
    const char *buffer;

    buffer = vptr;
    nleft  = n;

    while ( nleft > 0 ) {
	if ( (nwritten = write(sockd, buffer, nleft)) <= 0 ) {
	    if ( errno == EINTR )
		nwritten = 0;
	    else {
		log_string("Error in Writeline()");
		exit(1);
	    }
	}
	nleft  -= nwritten;
	buffer += nwritten;
    }

    return n;
}

//
// handle whatever request the socket is making
void webSocketHandle(WEB_SOCKET *sock) {
  static char get[SMALL_BUFFER];
  static char key[SMALL_BUFFER];
  char    *argstr = NULL;
  BUFFER     *buf = newBuffer(MAX_BUFFER);
  HASHTABLE *args = newHashtable();

  // clear everything
  *get = *key = '\0';

  // parse our key
  two_args(sock->inbuf, get, key);
  // replace the arg separator with a space, and then one_arg out the arguments
  if(replace_char(key, '?', ' '))
    argstr = strdup(one_arg(key+1, key));
  else
    one_arg(key+1, key);

  // parse out our argument string
  if(argstr != NULL) {
    char one_pair[SMALL_BUFFER];
    char  one_key[SMALL_BUFFER];
    char  one_val[SMALL_BUFFER];
    char *leftover = argstr;

    // separate all of our key:val pairs by spaces so we can one_arg them
    replace_char(argstr, '&', ' ');

    do { 
      // clear our buffers
      *one_pair = *one_key = *one_val = '\0';

      // parse out a pair, and replace the assigner 
      // with a space so we can two_args them apart
      leftover = one_arg(leftover, one_pair);
      replace_char(one_pair, '=', ' ');
      two_args(one_pair, one_key, one_val);

      // see if we need to put in the new key:val pair
      if(*one_key && *one_val && !hashIn(args, one_key))
	hashPut(args, one_key, strdup(one_val));
    } while(*leftover != '\0');
  }

  // Send response HTTP Headers
  char buffer[100];

  sprintf(buffer, "HTTP/1.0 200 OK\r\n");
  Writeline(sock->control, buffer, strlen(buffer));

  Writeline(sock->control, "Server: NereaMud v1.0\r\n", 24);
  Writeline(sock->control, "Content-Type: text/html\r\n", 25);
  Writeline(sock->control, "\r\n", 2);

  // find our function
  BUFFER *(* func)(HASHTABLE *args) = hashGet(query_table, key);
  BUFFER                   *content = NULL;

  // call it if it exists
  if(func == NULL || (content = func(args)) == NULL)
    bprintf(buf, "<html><body>Your request for %s was not found</body></html>",
	    key);
  else {
    // replace all of the colors and returns in the buf
    bufferASCIIHTML(content);

    // generate the output, with all of its required tags
    bprintf(buf, 
	    "<html><body bgcolor=\"black\" text=\"green\">"
	    "<font face=\"courier\">"
	    "%s"
	    "</font>"
	    "</body></html>", bufferString(content));
  }

  // if we had content, delete it now
  if(content != NULL)
    deleteBuffer(content);

  // send out the buf contents
  send(sock->control, bufferString(buf), strlen(bufferString(buf)), 0);

  // clean up our mess
  deleteBuffer(buf);
  if(argstr) free(argstr);
  if(hashSize(args) > 0) {
    const char    *h_key = NULL;
    char          *h_val = NULL;
    HASH_ITERATOR *arg_i = newHashIterator(args);
    ITERATE_HASH(h_key, h_val, arg_i) {
      free(h_val);
    } deleteHashIterator(arg_i);
  }
  deleteHashtable(args);
}

//
// A wrapper function for build_who to be usable in the web server
BUFFER *build_who_html(HASHTABLE *args) {
  return build_who();
}

//
// the main loop for our web server
void webserver_loop(void *owner, void *data, char *arg) {
  WEB_SOCKET  *conn = NULL;
  struct timeval tv = { 0, 0 }; // we don't wait for any action.
  fd_set read_fd;

  // get our sets all done up
  FD_ZERO(&read_fd);
  FD_SET(web_control, &read_fd);

  // check to see if something happens
  select(FD_SETSIZE, &read_fd, NULL, NULL, &tv);

  // check for a new connection
  if(FD_ISSET(web_control, &read_fd)) {
    conn = newWebSocket();
    unsigned int socksize;

    // try to accept the connection
    if((conn->control = accept(web_control, (struct sockaddr *)&conn->addr, 
			       &socksize)) > 0) {
      listQueue(web_descs, conn);
      FD_SET(conn->control, &read_fd);
    }
  }


  // do input handling
  LIST_ITERATOR *conn_i = newListIterator(web_descs);
  ITERATE_LIST(conn, conn_i) {
    if(FD_ISSET(conn->control, &read_fd)) {
      int in_len = read(conn->control, conn->inbuf + conn->buf_len, 
			MAX_INPUT_LEN - conn->buf_len - 1);
      if(in_len > 0) {
	conn->buf_len += in_len;
	conn->inbuf[conn->buf_len] = '\0';
      }
      else if(in_len < 0) {
	webSocketClose(conn);
	listRemove(web_descs, conn);
	deleteWebSocket(conn);
      }
    }
  } deleteListIterator(conn_i);

  // do output handling
  conn_i = newListIterator(web_descs);
  ITERATE_LIST(conn, conn_i) {
    // which version are we dealing with, and do we have a request terminator?
    // If we haven't gotten the terminator yet, don't handle or close the socket
    if( ( strstr(conn->inbuf, "HTTP/1.") && strstr(conn->inbuf, "\r\n\r\n")) ||
	(!strstr(conn->inbuf, "HTTP/1.") && strstr(conn->inbuf, "\n"))) {
      webSocketHandle(conn);
      webSocketClose(conn);
      listRemove(web_descs, conn);
      deleteWebSocket(conn);
    }
  } deleteListIterator(conn_i);
}



//*****************************************************************************
// implementation of webserver.h
//*****************************************************************************
void init_webserver(void) {
  struct sockaddr_in my_addr;
  int sockfd, reuse = 1, ret;

  log_string("init_webserver starting");

  // grab a socket
  sockfd = socket(AF_INET, SOCK_STREAM, 0);

  // set its values
  my_addr.sin_family      = AF_INET;
  my_addr.sin_addr.s_addr = INADDR_ANY;
  my_addr.sin_port        = htons(WEB_PORT);

  // fix thread problems?
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int)) == -1) {
    log_string("Webserver error in setsockopt()");
    exit(1);
  }

  // bind the port
  ret = bind(sockfd, (struct sockaddr *) &my_addr, sizeof(struct sockaddr));
  if (ret<0) {
	log_string("Error while binding webserver to socket");
        exit(1);
  }

  // start listening for connections
  ret = listen(sockfd, 3);
  if (ret) {
	log_string("Error listening on webserver socket");
        exit(1);
  }

  // set the socket control
  web_control = sockfd;

  // set up our list of connected sockets, and get the updater rolling
  web_descs   = newList();
  query_table = newHashtable();
  start_update(NULL, 0.1 SECOND, webserver_loop, NULL, NULL, NULL);

  // set up our basic queries
  add_query("who", build_who_html);
  log_string("init_webserver done");

}

void finalize_webserver(void) {
  WEB_SOCKET      *conn = NULL;
  LIST_ITERATOR *conn_i = newListIterator(web_descs);
  ITERATE_LIST(conn, conn_i) {
    close(conn->control);
  } deleteListIterator(conn_i);
  close(web_control);
}

void add_query(const char *key, BUFFER *(* func)(HASHTABLE *args)) {
  hashPut(query_table, key, func);
}
