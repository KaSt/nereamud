#ifndef WEBSERVER_H
#define WEBSERVER_H
//*****************************************************************************
//
// webserver.h
//
// This is a module that opens an HTTP server on another port that allows 
// people to request various sorts of information. Useful for displaying online
// who lists, and that sort of junk.
//
//*****************************************************************************

//
// the port we open up on. Some modules might need this
#define WEB_PORT                4072

//
// prepare our webserver for use
void init_webserver(void);

//
// close down the socket our webserver is using
void finalize_webserver(void);

//
// Add a query to the webserver. The key value is what the client is trying
// to get, be it who, summary, motd, greeting, or whatnot. func is a function
// that builds the query into a buffer and returns it. BUFFER will be deleted
// after, and should not be needed permenantly by func(). args is a mapping from
// key:val for arguments supplied to the query. Does not need to be used by
// every function.
void add_query(const char *key, BUFFER *(* func)(HASHTABLE *args));

extern BUFFER *build_who(void);

#endif // WEBSERVER_H
