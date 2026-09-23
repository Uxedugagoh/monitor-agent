#include <httplib.h>
#include <nlohmann/json.hpp>
#include <iostream>

int main()
{
    httplib::Server server;
    server.set_payload_max_length(1024 * 1024);
    server.Post("/", [](const httplib::Request& request, httplib::Response& response) {
        const auto packet = nlohmann::json::parse(request.body, nullptr, false);
        if (request.get_header_value("Content-Type") != "application/json" ||
            packet.is_discarded() || !packet.contains("payload") || !packet["payload"].is_array())
        {
            response.status = 400;
            return;
        }
        std::cout << packet.dump(2) << std::endl;
        response.status = 204;
    });
    std::cout << "Demo receiver: http://127.0.0.1:8080/ (Ctrl+C to stop)\n";
    if (!server.listen("127.0.0.1", 8080))
    {
        std::cerr << "Cannot listen on port 8080\n";
        return 1;
    }
}
