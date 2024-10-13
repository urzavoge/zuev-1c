#include "httplib.h"
#include "json.hpp"

#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <mutex>
#include <unordered_map>

using json = nlohmann::json;

class Checker {
public:

    bool CheckSecret(size_t secret) {
        if (known_.find(secret) != known_.end()) {
            return false;
        }
        if (secret % 2 == 0) {
            known_[secret] = 0;
            return true;
        }
        return false;
    }

private:
    std::unordered_map<size_t, size_t> known_;
};

bool CheckSecret(size_t secret) {
    return secret % 2;
}

namespace NExperiment {
    static char* self_ = nullptr;
}

class Experiment {
public:

    void RegisterUser(size_t id) {
        predictions_[id] = {};
    }

    bool IsRegistered(size_t id) {
        return predictions_.find(id) != predictions_.end();
    }

    void AddPrediction(size_t id, int num) {
        predictions_[id].push_back(num);
    }

    std::string GetPredictions(size_t id) {
        std::stringstream ss;
        for (int i : predictions_[id]) {
            ss << i << " ";
        }

        return ss.str();
    }

    void Flush(std::unordered_map<size_t, std::vector<int>>* predictions) {
        for (const auto& [id, vect] : predictions_) {
            for (int i : vect) {
                (*predictions)[id].push_back(i);
            }
        }
    }

    static bool IsActive() {
        return NExperiment::self_ != nullptr;
    }

    static void Init() {
        NExperiment::self_ = reinterpret_cast<char*>(new Experiment{});
    }

    static void Destoy() {
        delete NExperiment::self_;
    }

    static Experiment* Get() {
        return reinterpret_cast<Experiment*>(NExperiment::self_);
    }

private:
    std::unordered_map<size_t, std::vector<int>> predictions_;
};

class HttpServer {
public:

    struct User {
        size_t Id;
        std::string Address;
    };

    size_t Push(std::string address) {
        std::lock_guard<std::mutex> lock(mtx_);

        size_t id = users_.size();
        users_.push_back({
            .Id = id,
            .Address = std::move(address)
        });
        return id;
    }

    void Start() {
        std::lock_guard<std::mutex> lock(mtx_);
        Experiment::Init();

        for (auto& user : users_) {
            Experiment::Get()->RegisterUser(user.Id);

            httplib::Client cli(user.Address);
            auto _ = cli.Post("/notify", "Experiment is started!", "text/plain");
        }
    }

    void Stop() {
        Experiment::Get()->Flush(&stat_);
        Experiment::Destoy();
    }


    void RegisterUser(const httplib::Request& req, httplib::Response& res) {
        auto it = req.headers.find("Host");
        std::string domain;
        if (it != req.headers.end()) {
            domain = it->second;
        }
        json request;
        try { 
            request = json::parse(req.body);
        } catch (json::exception&) {
            res.status = 400;
            return;
        }

        size_t id = Push(domain + ":" + std::string(request["socket"]));

        res.status = 200;
        json response;
        response["id"] = id;
        res.set_content(response.dump(), "application/json");
        
    }

    void RegisterPrediction(const httplib::Request& req, httplib::Response& res) {
        json request;
        try {
            request = json::parse(req.body);
        } catch (json::exception&) {
            res.status = 400;
            return;
        }

        int pred = request["pred"];
        size_t id = request["id"];

        std::lock_guard<std::mutex> lock(exp_mtx_);
        if (!Experiment::IsActive() || !Experiment::Get()->IsRegistered(id)) {
            res.status = 400;
            return;
        }

        Experiment::Get()->AddPrediction(id, pred);

        res.status = 200;
    }

    void GetPredictions(const httplib::Request& req, httplib::Response& res) {
        json request;
        try {
            request = json::parse(req.body);
        } catch (json::exception&) {
            res.status = 400;
            return;
        }

        size_t id = request["id"];

        std::lock_guard<std::mutex> lock(exp_mtx_);
        if (!Experiment::IsActive() || !Experiment::Get()->IsRegistered(id)) {
            res.status = 400;
            return;
        }

        res.status = 200;
        json response;
        response["predictions"] = Experiment::Get()->GetPredictions(id);
        res.set_content(response.dump(), "application/json");
    }

    void StartExperiment(const httplib::Request& req, httplib::Response& res) {
        json request;
        try {
            request = json::parse(req.body);
        } catch (json::exception&) {
            res.status = 400;
            return;
        }

        size_t secret = std::stoi(std::string(request["secret"]));
        if (!checker_.CheckSecret(secret)) {
            res.status = 400;
            return;
        }

        std::lock_guard<std::mutex> lock(exp_mtx_);
        if (Experiment::IsActive()) {
            res.status = 400;
            return;
        }

        Start();
        res.status = 200;
    }

    void StopExperiment(const httplib::Request& req, httplib::Response& res) {
        json request;
        try {
            request = json::parse(req.body);
        } catch (json::exception&) {
            res.status = 400;
            return;
        }

        size_t secret = std::stoi(std::string(request["secret"]));
        if (!checker_.CheckSecret(secret)) {
            res.status = 400;
            return;
        }

        std::lock_guard<std::mutex> lock(exp_mtx_);
        if (!Experiment::IsActive()) {
            res.status = 400;
            return;
        }

        Stop();
        res.status = 200;
    }

    void AnswerToUser(const httplib::Request& req, httplib::Response& res) {
        json request;
        try {
            request = json::parse(req.body);
        } catch (json::exception&) {
            res.status = 400;
            return;
        }

        size_t secret = std::stoi(std::string(request["secret"]));
        if (!checker_.CheckSecret(secret)) {
            res.status = 400;
            return;
        }

        std::lock_guard<std::mutex> lock(exp_mtx_);
        if (!Experiment::IsActive()) {
            res.status = 400;
            return;
        }

        size_t id = std::stoi(std::string(request["id"]));
        std::string ans = request["answer"];

        httplib::Client cli(users_[id].Address);
        auto _ = cli.Post("/notify", ans, "text/plain");

        res.status = 200;
    }

    void GetWaiters(const httplib::Request& req, httplib::Response& res) {
        json request;
        try {
            request = json::parse(req.body);
        } catch (json::exception&) {
            res.status = 400;
            return;
        }

        size_t secret = std::stoi(std::string(request["secret"]));
        if (!checker_.CheckSecret(secret)) {
            res.status = 400;
            return;
        }
        
        std::lock_guard<std::mutex> lock(exp_mtx_);
        if (!Experiment::IsActive()) {
            res.status = 400;
            return;
        }

        json response;
        for (auto& user : users_) {
            if (Experiment::Get()->IsRegistered(user.Id)) {
                response[std::to_string(user.Id)] = Experiment::Get()->GetPredictions(user.Id);
            }
        }

        for (auto& user : users_) {
            if (Experiment::Get()->IsRegistered(user.Id)) {
                response[std::to_string(user.Id)] = Experiment::Get()->GetPredictions(user.Id);
            }
        }

        res.status = 200;
        res.set_content(response.dump(), "application/json");
    }

    void GetStat(const httplib::Request& req, httplib::Response& res) {
        json request;
        try {
            request = json::parse(req.body);
        } catch (json::exception&) {
            res.status = 400;
            return;
        }
        
        std::lock_guard<std::mutex> lock(exp_mtx_);
        if (!Experiment::IsActive()) {
            res.status = 400;
            return;
        }

        size_t secret = std::stoi(std::string(request["secret"]));
        if (!checker_.CheckSecret(secret)) {
            res.status = 400;
            return;
        }

        json response;

        for (auto& user : users_) {
            if (Experiment::Get()->IsRegistered(user.Id)) {
                response["Current"][std::to_string(user.Id)] = Experiment::Get()->GetPredictions(user.Id);
            }
        }

        for (const auto& [id, vect] : stat_) {
            std::stringstream ss;
            for (int i : vect) {
                ss << i << " ";
            }
            response["Old"][std::to_string(id)] = ss.str();
        }

        res.status = 200;
        res.set_content(response.dump(), "application/json");
    }


private:

    std::mutex mtx_;
    std::mutex exp_mtx_;
    std::vector<User> users_;

    Checker checker_;

    std::unordered_map<size_t, std::vector<int>> stat_;
};

int main(int argc, char* argv[]) {
    httplib::Server svr;
    svr.new_task_queue = [=] { return new httplib::ThreadPool(std::atoi(argv[1]), std::atoi(argv[2])); };

    HttpServer server;
    svr.Post("/user/register", [&](const httplib::Request& req, httplib::Response& res) {
        server.RegisterUser(req, res);
    });

    svr.Post("/user/predict", [&](const httplib::Request& req, httplib::Response& res) {
        server.RegisterPrediction(req, res);
    });

    svr.Get("/user/get", [&](const httplib::Request& req, httplib::Response& res) {
        server.GetPredictions(req, res);
    });

    svr.Post("/admin/start", [&](const httplib::Request& req, httplib::Response& res) {
        server.StartExperiment(req, res);
    });

    svr.Post("/admin/stop", [&](const httplib::Request& req, httplib::Response& res) {
        server.StartExperiment(req, res);
    });

    svr.Post("/admin/answer", [&](const httplib::Request& req, httplib::Response& res) {
        server.AnswerToUser(req, res);
    });

    svr.Get("/admin/get", [&](const httplib::Request& req, httplib::Response& res) {
        server.GetWaiters(req, res);
    });

    svr.Get("/admin/stat", [&](const httplib::Request& req, httplib::Response& res) {
        server.GetStat(req, res);
    });

    svr.listen("0.0.0.0", 8080);

}
