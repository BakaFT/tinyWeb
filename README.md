# tinyWeb

This is a ~200 lines HTTP Server implemented by C language.

It supports:

- static content
- basic mime types
- GET method
- Status code
  - 200 OK
  - 501 Not Implemented
  - 404 Not Found

# To run it

Using your favorite terminal & shell.

Clone the repo

```bash
git clone https://github.com/BakaFT/tinyWeb.git
```

Compile 

```bash
cd tinyWeb
gcc web.c -o web
```

Run

```bash
./web <port>
```



