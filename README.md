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

### Plans

I want to add the following features to the project:
- Arguments for the directory to use and port to serve the app to.
- Directory listings (Frontend in C).
- 404 page for not found paths.
- More error checks to make the app a bit friendlier.
