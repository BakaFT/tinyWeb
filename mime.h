#define ARRAYSIZE 10 //Mime types count

typedef struct Map
{
    char *key;
    char *value;
} Mapnode;

Mapnode mimelist[ARRAYSIZE] = {
    {".mp3", "audio/mpeg"},
    {".gif", "image/gif"},
    {".jpg", "image/jpeg"},
    {".png", "image/png"},
    {".svg", "image/svg+xml"},
    {".css", "text/css"},
    {".json","application/json"},
    {".html", "text/html"},
    {".txt", "text/plain"},
    {".xml", "text/xml"},
    {".ico","image/x-icon"}
};