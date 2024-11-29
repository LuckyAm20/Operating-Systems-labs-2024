#pragma once

#include "../connections/connection_fabric.hpp"

void client_signal_handler(int, siginfo_t *, void *);

class ClientLogic
{
private:
    int host_pid;
    int pid;
    std::unique_ptr<Connection> host_to_client;
    std::unique_ptr<Connection> client_to_host;
    std::unique_ptr<Connection> general_host_to_client;
    std::unique_ptr<Connection> general_client_to_host;
    struct sigaction signal_handler;
    ClientLogic(ConnectionType type, const std::string &host_pid_path)
    {
        std::fstream file(host_pid_path);
        if (!file)
            throw std::runtime_error("No config file" + std::string(std::filesystem::absolute(host_pid_path)));
        file >> host_pid;
        file.close();
        pid = getpid();

        signal_handler.sa_sigaction = client_signal_handler;
        signal_handler.sa_flags = SA_SIGINFO;

        if (sigaction(SIGUSR1, &signal_handler, nullptr) == -1 || 
            sigaction(SIGUSR2, &signal_handler, nullptr) == -1)
            throw std::runtime_error("Failed to register signal handler");

        kill(host_pid, SIGUSR1);

        sem_t *semaphore = sem_open(("sem" + ConnectionFabric::GenerateFilename(type, host_pid, pid)).c_str(), O_CREAT, 0777, 0);
        struct timespec tm;
        int s;
        if (clock_gettime(CLOCK_REALTIME, &tm) == -1)
            throw std::runtime_error("Failed to get real time");
        tm.tv_sec += 5;
        while ((s = sem_timedwait(semaphore, &tm)) == -1 && errno == EINTR)
            continue;

        host_to_client = std::move(ConnectionFabric::CreateConnection(type, ConnectionFabric::GenerateFilename(type, host_pid, pid), false));
        client_to_host = std::move(ConnectionFabric::CreateConnection(type, ConnectionFabric::GenerateFilename(type, pid, host_pid), false));
        general_host_to_client = std::move(ConnectionFabric::CreateConnection(type, ConnectionFabric::GenerateGeneralFilename(type, host_pid, pid), false));
        general_client_to_host = std::move(ConnectionFabric::CreateConnection(type, ConnectionFabric::GenerateGeneralFilename(type, pid, host_pid), false));

        sem_unlink(("sem" + ConnectionFabric::GenerateFilename(type, host_pid, pid)).c_str());
    }

public:
    ClientLogic(const ClientLogic &) = delete;
    ClientLogic &operator=(const ClientLogic &) = delete;
    ClientLogic(ClientLogic &&) = delete;
    ClientLogic &operator=(ClientLogic &&) = delete;
    ~ClientLogic() = default;

    friend void client_signal_handler(int, siginfo_t *, void *);
    static ClientLogic &get_instance(ConnectionType type, const std::string &host_pid_path)
    {
        static ClientLogic instance(type, host_pid_path);
        return instance;
    }
    bool send_to_host(const std::string& message)
    {
        bool f = client_to_host->Write(message);
        kill(host_pid, SIGUSR1);
        return f;
    }

    bool read_from_host(std::string& message)
    {
        return host_to_client->Read(message);
    }

    bool send_to_host_general(const std::string &message)
    {
        bool f = general_client_to_host->Write(message);
        kill(host_pid, SIGUSR2);
        return f;
    }

    bool read_from_host_general(std::string &message)
    {
        return general_host_to_client->Read(message);
    }
};