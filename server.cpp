#include "httplib.h"
#include "json.hpp"

#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <mutex>
#include <unordered_map>

#include "thread_pool.h"

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
        std::lock_guard<std::mutex> lock(mtx_);

        return predictions_.find(id) != predictions_.end();
    }

    void AddPrediction(size_t id, int num) {
        std::lock_guard<std::mutex> lock(mtx_);

        predictions_[id].push_back(num);
    }

    std::string GetPredictions(size_t id) {
        std::lock_guard<std::mutex> lock(mtx_);

        std::stringstream ss;
        for (int i : predictions_[id]) {
            ss << i << " ";
        }

        return ss.str();
    }

    void Flush(std::unordered_map<size_t, std::vector<int>>* predictions) {
        std::lock_guard<std::mutex> lock(mtx_);

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

    std::mutex mtx_;
    std::unordered_map<size_t, std::vector<int>> predictions_;

    bool active_;

};

class HttpServer {
public:

    struct User {
        size_t Id;
        std::string Address;
    };

    void RegisterUser(const httplib::Request& req, httplib::Response& res) {
        pool_.enqueue([=]() mutable {
            RegisterUser_(req, res);
        });
    }

    void RegisterPrediction(const httplib::Request& req, httplib::Response& res) {
        pool_.enqueue([&, this](){
            RegisterPrediction_(req, res);
        });
    }

    void GetPredictions(const httplib::Request& req, httplib::Response& res) {
        pool_.enqueue([&, this](){
            GetPredictions_(req, res);
        });
    }

    void StartExperiment(const httplib::Request& req, httplib::Response& res) {
        pool_.enqueue([&, this](){
            StartExperiment_(req, res);
        });
    }

    void StopExperiment(const httplib::Request& req, httplib::Response& res) {
        pool_.enqueue([&, this](){
            StopExperiment_(req, res);
        });
    }

    void AnswerToUser(const httplib::Request& req, httplib::Response& res) {
        pool_.enqueue([&, this](){
            AnswerToUser_(req, res);
        });
    }

    void GetWaiters(const httplib::Request& req, httplib::Response& res) {
        pool_.enqueue([&, this](){
            GetWaiters_(req, res);
        });
    }

    void GetStat(const httplib::Request& req, httplib::Response& res) {
        pool_.enqueue([&, this](){
            GetStat_(req, res);
        });
    }
    


private:

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
        std::lock_guard<std::mutex> lock(mtx_);

        Experiment::Get()->Flush(&stat_);
        Experiment::Destoy();
    }


    void RegisterUser_(const httplib::Request& req, httplib::Response& res) {
        try {
            auto it = req.headers.find("Host");
            std::string domain;
            if (it != req.headers.end()) {
                domain = it->second;
            }
            auto requset = json::parse(req.body);

            size_t id = Push(domain + ":" + std::string(requset["socket"]));

            res.status = 200;
            json response;
            response["id"] = id;
            res.set_content(response.dump(), "application/json");
        } catch (std::exception&) {
            res.status = 400;
        }
    }

    void RegisterPrediction_(const httplib::Request& req, httplib::Response& res) {
        json requset;
        try {
            requset = json::parse(req.body);
        } catch (std::out_of_range&) {
            res.status = 400;
            return;
        }

        int pred = requset["pred"];
        size_t id = requset["id"];

        if (!Experiment::IsActive() || !Experiment::Get()->IsRegistered(id)) {
            res.status = 400;
            return;
        }

        res.status = 200;
    }

    void GetPredictions_(const httplib::Request& req, httplib::Response& res) {
        json requset;
        try {
            requset = json::parse(req.body);
        } catch (std::out_of_range&) {
            res.status = 400;
            return;
        }

        size_t id = requset["id"];

        if (!Experiment::IsActive() || !Experiment::Get()->IsRegistered(id)) {
            res.status = 400;
            return;
        }

        res.status = 200;
        json response;
        response["predictions"] = Experiment::Get()->GetPredictions(id);
        res.set_content(response.dump(), "application/json");
    }

    void StartExperiment_(const httplib::Request& req, httplib::Response& res) {
        json requset;
        try {
            requset = json::parse(req.body);
        } catch (std::out_of_range&) {
            res.status = 400;
            return;
        }

        if (Experiment::IsActive()) {
            res.status = 400;
            return;
        }

        size_t secret = std::stoi(std::string(requset["secret"]));
        if (!checker_.CheckSecret(secret)) {
            res.status = 400;
            return;
        }

        Start();
        res.status = 200;
    }

    void StopExperiment_(const httplib::Request& req, httplib::Response& res) {
        json requset;
        try {
            requset = json::parse(req.body);
        } catch (std::out_of_range&) {
            res.status = 400;
            return;
        }

        if (!Experiment::IsActive()) {
            res.status = 400;
            return;
        }

        size_t secret = std::stoi(std::string(requset["secret"]));
        if (!checker_.CheckSecret(secret)) {
            res.status = 400;
            return;
        }

        Stop();
        res.status = 200;
    }

    void AnswerToUser_(const httplib::Request& req, httplib::Response& res) {
        json requset;
        try {
            requset = json::parse(req.body);
        } catch (std::out_of_range&) {
            res.status = 400;
            return;
        }

        if (!Experiment::IsActive()) {
            res.status = 400;
            return;
        }

        size_t secret = std::stoi(std::string(requset["secret"]));
        if (!checker_.CheckSecret(secret)) {
            res.status = 400;
            return;
        }

        size_t id = std::stoi(std::string(requset["id"]));
        std::string ans = requset["answer"];

        std::lock_guard<std::mutex> lock(mtx_);
        httplib::Client cli(users_[id].Address);
        auto _ = cli.Post("/notify", ans, "text/plain");

        res.status = 200;
    }

    void GetWaiters_(const httplib::Request& req, httplib::Response& res) {
        json requset;
        try {
            requset = json::parse(req.body);
        } catch (std::out_of_range&) {
            res.status = 400;
            return;
        }

        if (!Experiment::IsActive()) {
            res.status = 400;
            return;
        }

        size_t secret = std::stoi(std::string(requset["secret"]));
        if (!checker_.CheckSecret(secret)) {
            res.status = 400;
            return;
        }

        json response;

        std::lock_guard<std::mutex> lock(mtx_);
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

    void GetStat_(const httplib::Request& req, httplib::Response& res) {
        json requset;
        try {
            requset = json::parse(req.body);
        } catch (std::out_of_range&) {
            res.status = 400;
            return;
        }

        if (!Experiment::IsActive()) {
            res.status = 400;
            return;
        }

        size_t secret = std::stoi(std::string(requset["secret"]));
        if (!checker_.CheckSecret(secret)) {
            res.status = 400;
            return;
        }

        json response;

        std::lock_guard<std::mutex> lock(mtx_);
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
    std::vector<User> users_;

    std::unordered_map<size_t, std::vector<int>> predictions_;

    ThreadPool pool_{4};

    Checker checker_;

    std::unordered_map<size_t, std::vector<int>> stat_;
};

int main() {
    httplib::Server svr;

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
