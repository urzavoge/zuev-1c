#include "httplib.h"
#include "json.hpp"

#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <mutex>
#include <unordered_map>

using json = nlohmann::json;

class User {
public:

    void Run(int argc, char* argv[]) {
        for (;;) {
            std::string command;
            std::cin >> command;

            if (command == "register") {
                httplib::Client cli(argv[2]);

                json req;
                req["host"] = "localhost:" + std::string(argv[1]);
                auto res = cli.Post("/user/register", req.dump(), "application/json");
                if (res && res->status == 200) {

                    auto result = json::parse(res->body);

                    Id_ = result["id"];
                    std::cout << result["id"] << '\n';
                } else {
                    std::cout << "Erorr\n";
                }
                
                continue;
            }

            if (command == "predict") {
                int num;
                std::cin >> num;

                httplib::Client cli(argv[2]);

                json req;
                req["id"] = Id_;
                req["pred"] = num;
                auto res = cli.Post("/user/predict", req.dump(), "application/json");
                if (res && res->status == 200) {
                    std::cout << "Ok\n";
                } else {
                    std::cout << "Erorr\n";
                }
                continue;
            }

            if (command == "see-my-predictions") {
                httplib::Client cli(argv[2]);

                json req;
                req["id"] = Id_;
                auto res = cli.Post("/user/predict", req.dump(), "application/json");
                if (res && res->status == 200) {
                    auto result = json::parse(res->body);
                    std::cout << result["predictions"] << '\n';
                } else {
                    std::cout << "Erorr\n";
                }
                continue;
            }
        }
    }


private:
    size_t Id_;
};

class Admin {
public:

    void Run(int argc, char* argv[]) {
        for (;;) {
            std::string command;
            std::cin >> command;

            if (command == "start") {
                httplib::Client cli(argv[2]);

                json req;
                req["secret"] = generator.Get();
                auto res = cli.Post("/admin/start", req.dump(), "application/json");
                if (res && res->status == 200) {
                    std::cout << "Ok\n" << '\n';
                } else {
                    std::cout << "Erorr\n";
                }
                
                continue;
            }

            if (command == "stop") {
                httplib::Client cli(argv[2]);

                json req;
                req["secret"] = generator.Get();
                auto res = cli.Post("/admin/stop", req.dump(), "application/json");
                if (res && res->status == 200) {
                    std::cout << "Ok\n" << '\n';
                } else {
                    std::cout << "Erorr\n";
                }
                
                continue;
            }

            if (command == "answer") {
                int id;
                std::cin >> id;

                std::string answer;
                std::cin >> answer;

                httplib::Client cli(argv[2]);

                json req;
                req["id"] = id;
                req["answer"] = answer;
                req["secret"] = generator.Get();

                
                auto res = cli.Post("/admin/answer", req.dump(), "application/json");
                if (res && res->status == 200) {
                    std::cout << "Ok\n";
                } else {
                    std::cout << "Erorr\n";
                }
                continue;
            }

            if (command == "get") {
                httplib::Client cli(argv[2]);

                json req;
                req["secret"] = generator.Get();

                auto res = cli.Post("/admin/get", req.dump(), "application/json");
                if (res && res->status == 200) {
                    std::cout << res->body << '\n';
                } else {
                    std::cout << "Erorr\n";
                }
                continue;
            }

            if (command == "statistic") {
                httplib::Client cli(argv[2]);

                json req;
                req["secret"] = generator.Get();

                auto res = cli.Post("/admin/stat", req.dump(), "application/json");
                if (res && res->status == 200) {
                    std::cout << res->body << '\n';
                } else {
                    std::cout << "Erorr\n";
                }
                continue;
            }

        }
    }


private:

    class Generator {
    public:
        size_t Get() {
            return ++seed;
        }

    private:
        size_t seed = 0;
    };

    Generator generator;
};

int main(int argc, char* argv[]) {

    std::thread server{[=](){
        httplib::Server svr;

        svr.Post("/notify", [&](const httplib::Request& req, httplib::Response& res) {
            std::cout << req.body << '\n';
        });

        svr.listen("0.0.0.0", std::atoi(argv[1]));
    }};

    std::string permission;
    std::cout << "Input yours permission:\n";
    std::cin >> permission;
    
    if (permission == "User") {
        User user;
        user.Run(argc, argv);
    }

    if (permission == "Admin") {
        std::string password;
        std::cout << "Enter Password:\n";
        std::cin >> password;

        if (password == "banana") {
            Admin admin;
            admin.Run(argc, argv);
        }
        
    }

    return 0;
}