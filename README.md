# HTTP Craft

In this project I want to learn how the HTTP protocol works, how to
parse the requests and how to build responses that the browser/curl can understand.

### Usage
clone and then:

```console
make # to compile
./server <port> <directory>
```

after you run it on a port you chose you can test using `curl` or your browser
for now it serves static files from the provided directory and it only support GET requests
but it is cool IMO

if the provided directory has `index.html` or `index.php` the server will serve it by default
else it lists the directory content
