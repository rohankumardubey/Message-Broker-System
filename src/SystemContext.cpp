//
// Created by mando on 12/01/21.
//

#include <iostream>
#include <boost/program_options.hpp>
#include <yaml-cpp/yaml.h>
#include "SystemContext.h"
#include "StatusLog.h"
#include <string>
#include <ctime>
namespace po = boost::program_options;
void ProcessAndLog(std::string client, std::string request_content, std::string type_of_request, Topic & topic);

SystemContext& SystemContext::GenerateContext(int argc, char** argv) {
    static SystemContext systemContext;

    po::options_description description("usage: eventflow [options]");
    description.add_options()
            ("help", "shows this help message")
            ("config", po::value<std::string>(),
                    "specify location of configuration file (Defaults to config.yaml)")
            ("port", po::value<uint16_t>(),
                    "specify port number for REST API (Defaults to 18080)")
            ("threads", po::value<uint16_t>(),
                    "specify number of threads for REST API (Defaults to the number of threads supported by "
                    "the hardware)")
            ("crt", po::value<std::string>(),
                    "specify SSL Public Key file location (Defaults to host.crt")
            ("key", po::value<std::string>(),
                    "specify SSL Private Key file location (Defaults to host.key")
            ;

    po::variables_map variablesMap;
    po::store(po::parse_command_line(argc, argv, description), variablesMap);
    po::notify(variablesMap);

    if (variablesMap.count("help")) {
        std::cout << description << std::endl;
        exit(1);
    }

    std::string configFile("config.yaml");
    if (variablesMap.count("config")) {
        configFile.assign(variablesMap["config"].as<std::string>());
    }

    const YAML::Node config = YAML::LoadFile(configFile);

    // Setting Port number for REST API
    if (variablesMap.count("port")) {
        systemContext.port_no = variablesMap["port"].as<uint16_t>();
    } else if (config["port"]){
        systemContext.port_no = config["port"].as<uint16_t>();
    } else {
        systemContext.port_no = 18080;
    }

    // Setting Number of threads for REST API
    if (variablesMap.count("threads")) {
        systemContext.num_api_threads = variablesMap["threads"].as<uint16_t>();
    } else if (config["threads"]){
        systemContext.num_api_threads = config["threads"].as<uint16_t>();
    } else {
        systemContext.num_api_threads = 0;
    }

    // Setting SSL Public Key file location
    if (variablesMap.count("crt")) {
        systemContext.crt_file_path.assign(variablesMap["crt"].as<std::string>());
    } else if (config["crt"]){
        systemContext.crt_file_path.assign(config["crt"].as<std::string>());
    } else {
        systemContext.crt_file_path.assign("host.crt");
    }

    // Setting SSL Private Key file location
    if (variablesMap.count("key")) {
        systemContext.key_file_path.assign(variablesMap["key"].as<std::string>());
    } else if (config["key"]){
        systemContext.key_file_path.assign(config["key"].as<std::string>());
    } else {
        systemContext.key_file_path.assign("host.key");
    }

    const YAML::Node& topics = config["topics"];
    std::vector<std::string> topic_names;
    std::vector<std::string> client_names;

    topic_names.reserve(topics.size());

    for (const auto & topic : topics) {
        topic_names.emplace_back(topic["name"].as<std::string>());
        for (int j = 0; j < topic["publishers"].size(); j++) {
            client_names.emplace_back(topic["publishers"][j].as<std::string>());
        }
        for (int j = 0; j < topic["subscribers"].size(); j++) {
            client_names.emplace_back(topic["subscribers"][j].as<std::string>());
        }
    }

    std::unique(topic_names.begin(), topic_names.end());
    std::unique(client_names.begin(), client_names.end());

    systemContext.accessList = std::make_unique<AccessList>(topic_names, client_names);

    for (const auto & topic : topics) {
        for (int j = 0; j < topic["publishers"].size(); j++) {
            client_names.emplace_back();
            systemContext.accessList->addAsPublisherOf(topic["publishers"][j].as<std::string>(),
                    topic["name"].as<std::string>());
        }
        for (int j = 0; j < topic["subscribers"].size(); j++) {
            client_names.emplace_back();
            systemContext.accessList->addAsSubscriberOf(topic["subscribers"][j].as<std::string>(),
                                                      topic["name"].as<std::string>());
        }
    }

    return systemContext;
}

void SystemContext::StartAPI() {
    CROW_ROUTE(app, "/auth")
    .methods("POST"_method)
    ([this](const crow::request& req) {
        auto body = crow::json::load(req.body);
        AddClient(req.ip_address, body["name"].s(), body["notify_on"].s());
        std::string client = body["name"].s();
        std::string request_content = "Request IP: "+req.ip_address+"\n"+ 
                                    "Status: Successful";
        std::string type_of_request = "Client Authentication";
        ProcessAndLog(client, request_content, type_of_request, topics.at("status_log"));
        return "Client Authenticated\n";
    });

    CROW_ROUTE(app, "/publish")
    .methods("POST"_method)
    ([this](const crow::request& req) {
        auto body = crow::json::load(req.body);
        std::string request_content = "Request IP: "+req.ip_address;
        std::string type_of_request = "Publish event on topic";
        Topic & stat_topic = topics.at("status_log");
        try 
        {
            string topic_name = body["topic"].s();
            request_content += "\nTopic: " + topic_name;
            Client& client = clients.at(req.ip_address);
            
            if (!accessList->isPublisherOf(client.name, topic_name)) {
                request_content += "\nStatus: Access Denied. Publish Unsuccessful."; 
                ProcessAndLog(client.name, request_content, type_of_request, stat_topic);        
                return crow::response(404, "Cannot publish to topic " + topic_name);
            }
            if (!body["topic"] || !body["event"]) {
                request_content += "\nStatus: Invalid request. Publish Unsuccessful."; 
                ProcessAndLog(client.name, request_content, type_of_request, stat_topic);        
                return crow::response(404, "Invalid Request");
            }
            try {
                Topic& topic = topics.at(topic_name);
                string msg = body["event"].s();
                topic.pub_event(Event(msg));
                request_content += "\nStatus: Publish Successful."; 
                ProcessAndLog(client.name, request_content, type_of_request, stat_topic);        
                return crow::response(200, "Event published on Topic " + topic_name);
            } 
            catch (const std::out_of_range ex) {
                request_content += "\nStatus: Topic does not exist. Publish Unsuccessful."; 
                ProcessAndLog(client.name, request_content, type_of_request, stat_topic);        
                return crow::response(404, "Topic does not exist.");
            }
        } catch (const std::out_of_range ex) {
            request_content += "\nStatus: Client Invalid. Publish Unsuccessful."; 
            ProcessAndLog("Invalid Client", request_content, type_of_request, stat_topic);        
            return crow::response(404, "Invalid client");
        }
    });

    CROW_ROUTE(app, "/sub")
    .methods("POST"_method)
    ([]() {
        // TODO: Add client as a subscriber to all the topics in the JSON object
        return "Subscription Successful\n";
    });

    CROW_ROUTE(app, "/<string>")
    .methods("GET"_method)
    ([this](const crow::request& req, const std::string& topic_name) {
        auto body = crow::json::load(req.body);
        crow::json::wvalue response;
        std::string client = body["name"].s();
        std::string request_content = "Request IP: "+req.ip_address;
        std::string type_of_req = "Fetch events from topic";
        Topic & stat_topic = topics.at("status_log");
        if (!(accessList->isSubscriberOf(body["name"].s(), topic_name))) {
            request_content += "\nStatus: Could not fetch events from this topic. Access Denied."; 
            ProcessAndLog(client, request_content, type_of_req, stat_topic);        
            response["event"] = "";
            response["error"] = "Cannot access topic " + topic_name;
            return response;
        }
        request_content += "\nStatus: Event Fetch Successful"; 
        ProcessAndLog(client, request_content, type_of_req, stat_topic);        
        response["event"] = topics[topic_name].get_event().message;
        response["error"] = "";
        return response;
    });

    CROW_ROUTE(app, "/disconnect")
    .methods("POST"_method)
    ([]() {
        // TODO: Disconnect client
        return "Disconnected\n";
    });

    app.port(port_no).multithreaded(num_api_threads).run();
//    app.port(port_no).multithreaded(num_api_threads).ssl_file(crt_file_path, key_file_path).run();
}

void SystemContext::AddClient(const std::string& ip_address, std::string name, std::string notif_port_no) {
    mutex_lock.lock();
    clients.insert({ip_address, Client(std::move(name), std::move(notif_port_no))});
    mutex_lock.unlock();
}

void ProcessAndLog(std::string client, std::string request_content, std::string type_of_request, Topic & topic)
{
    const time_t curr_time = time(0);
    std::string time_of_request = ctime(&curr_time);
    StatusLog log_obj = StatusLog(client, request_content, type_of_request, time_of_request);
    StatusLog::LogStatus(log_obj, topic);        
}
