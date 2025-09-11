#ifndef http_h
#define http_h

#define MCP_PORT 8100


extern void process_http();
extern int init_http();
extern void end_http();
extern void http_200_json(int cfd, const char* body);


#endif