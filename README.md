# HTTP Craft

In this project I want to learn how the HTTP protocol works, how to
parse the requests and how to build responses that the browser/curl command can
understand.

### Usage
clone and then:

```console
make # to compile
./server <port>
```

after you run it on port you can test using `curl` or your browser
for now it serve files from the current directory and only GET method

if the current directory has `index.html` or  `index.php`	the server will chose it,
if it doesn't have it list the directory content
