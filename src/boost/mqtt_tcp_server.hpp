#ifndef MQTTTCPSERVER_H_
#define MQTTTCPSERVER_H_

#include <boost/asio.hpp>
#include "connection.hpp"
#include "connection_manager.hpp"
#include "server.hpp"

class MqttTcpServer {
public:

    MqttTcpServer(const MqttTcpServer&) = delete;
    MqttTcpServer& operator=(const MqttTcpServer&) = delete;

    MqttTcpServer(int port = 1883, bool useCache = false);

    void run();

private:

    boost::asio::io_service _ioService;
    boost::asio::signal_set _signals;
    boost::asio::ip::tcp::acceptor _acceptor;
    ConnectionManager _connectionManager;
    boost::asio::ip::tcp::socket _socket;
    MqttServer<Connection> _mqttServer;

    void doAccept();
    void doAwaitStop();
};


#endif // SERVER_H_
