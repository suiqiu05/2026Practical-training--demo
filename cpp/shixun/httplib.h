#ifndef CPPHTTPLIB_H
#define CPPHTTPLIB_H

#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <ctime>

namespace httplib {

    using namespace std;

    struct Request {
        string method;
        string path;
        map<string, string> headers;
        string body;
    };

    struct Response {
        int status = 200;
        map<string, string> headers;
        string body;

        void set_content(const string& content, const string& content_type) {
            body = content;
            headers["Content-Type"] = content_type;
            headers["Content-Length"] = to_string(body.size());
        }
    };

    class Server {
    public:
        using Handler = function<void(const Request&, Response&)>;

        Server() = default;

        void Post(const string& path, Handler handler) {
            post_handlers_[path] = handler;
        }

        void Get(const string& path, Handler handler) {
            get_handlers_[path] = handler;
        }

        bool listen(const string& host, int port) {
            cout << "Server listening on " << host << ":" << port << endl;
            return true;
        }

        void handle_request(const Request& req, Response& res) {
            if (req.method == "POST" && post_handlers_.count(req.path)) {
                post_handlers_[req.path](req, res);
            }
            else if (req.method == "GET" && get_handlers_.count(req.path)) {
                get_handlers_[req.path](req, res);
            }
            else {
                res.status = 404;
                res.set_content("Not Found", "text/plain");
            }
        }

    private:
        map<string, Handler> post_handlers_;
        map<string, Handler> get_handlers_;
    };

} // namespace httplib

#endif