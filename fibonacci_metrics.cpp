#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <mutex>
#include <sstream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>
#include <map>
#include <regex>

// Глобальный флаг для остановки сервера
volatile sig_atomic_t running = 1;

void signal_handler(int sig) {
    running = 0;
}

/**
 * Класс для вычисления чисел Фибоначчи
 */
class FibonacciCalculator {
public:
    static long long calculate(int n) {
        if (n < 0) {
            throw std::invalid_argument("n must be non-negative");
        }
        if (n > 50) {
            throw std::invalid_argument("n too large (max 50)");
        }
        
        if (n == 0) return 0;
        if (n == 1) return 1;
        
        long long prev = 0;
        long long curr = 1;
        long long next;
        
        for (int i = 2; i <= n; ++i) {
            next = prev + curr;
            prev = curr;
            curr = next;
        }
        
        return curr;
    }
};

/**
 * HTTP сервер для метрик Prometheus
 */
class MetricsServer {
private:
    int server_fd;
    std::thread server_thread;
    std::mutex metrics_mutex;
    
    // Метрики
    long long total_requests = 0;
    long long total_calculation_time_ms = 0;
    long long current_connections = 0;
    std::map<int, long long> request_counts; // Распределение запросов по n
    
    std::string generateMetrics() {
        std::lock_guard<std::mutex> lock(metrics_mutex);
        std::stringstream ss;
        
        // Основной счетчик запросов
        ss << "# HELP fibonacci_requests_total Total number of Fibonacci calculations\n";
        ss << "# TYPE fibonacci_requests_total counter\n";
        ss << "fibonacci_requests_total " << total_requests << "\n\n";
        
        // Суммарное время вычислений
        ss << "# HELP fibonacci_calculation_time_ms_total Total calculation time in milliseconds\n";
        ss << "# TYPE fibonacci_calculation_time_ms_total counter\n";
        ss << "fibonacci_calculation_time_ms_total " << total_calculation_time_ms << "\n\n";
        
        // Текущие соединения (gauge)
        ss << "# HELP fibonacci_current_connections Current number of connections\n";
        ss << "# TYPE fibonacci_current_connections gauge\n";
        ss << "fibonacci_current_connections " << current_connections << "\n\n";
        
        // Распределение запросов по значениям n (гистограмма)
        ss << "# HELP fibonacci_requests_by_n Distribution of requests by n value\n";
        ss << "# TYPE fibonacci_requests_by_n counter\n";
        for (const auto& [n, count] : request_counts) {
            ss << "fibonacci_requests_by_n{n=\"" << n << "\"} " << count << "\n";
        }
        
        return ss.str();
    }
    
    std::string parsePostBody(const std::string& request) {
        size_t body_pos = request.find("\r\n\r\n");
        if (body_pos != std::string::npos) {
            std::string body = request.substr(body_pos + 4);
            // Удаляем пробелы и символы перевода строки
            body.erase(std::remove(body.begin(), body.end(), '\r'), body.end());
            body.erase(std::remove(body.begin(), body.end(), '\n'), body.end());
            return body;
        }
        return "";
    }
    
    void handleClient(int client_fd) {
        // Увеличиваем счетчик соединений
        {
            std::lock_guard<std::mutex> lock(metrics_mutex);
            current_connections++;
        }
        
        char buffer[4096] = {0};
        read(client_fd, buffer, 4096);
        std::string request(buffer);
        
        std::string response;
        
        // Обработка POST /run
        if (request.find("POST /run") != std::string::npos) {
            std::string body = parsePostBody(request);
            
            try {
                int n = std::stoi(body);
                
                auto start = std::chrono::high_resolution_clock::now();
                long long result = FibonacciCalculator::calculate(n);
                auto end = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
                
                {
                    std::lock_guard<std::mutex> lock(metrics_mutex);
                    total_requests++;
                    total_calculation_time_ms += duration.count();
                    request_counts[n]++;
                }
                
                response = "HTTP/1.1 200 OK\r\n";
                response += "Content-Type: text/plain\r\n";
                response += "Connection: close\r\n\r\n";
                response += std::to_string(result);
                
            } catch (const std::exception& e) {
                response = "HTTP/1.1 400 Bad Request\r\n";
                response += "Content-Type: text/plain\r\n";
                response += "Connection: close\r\n\r\n";
                response += "Error: " + std::string(e.what());
            }
        }
        // Обработка GET /metrics
        else if (request.find("GET /metrics") != std::string::npos) {
            response = "HTTP/1.1 200 OK\r\n";
            response += "Content-Type: text/plain; version=0.0.4\r\n";
            response += "Connection: close\r\n\r\n";
            response += generateMetrics();
        }
        // Обработка GET /health или GET /
        else if (request.find("GET /health") != std::string::npos || 
                 request.find("GET / ") != std::string::npos ||
                 request.find("GET / HTTP") != std::string::npos) {
            response = "HTTP/1.1 200 OK\r\n";
            response += "Content-Type: text/plain\r\n";
            response += "Connection: close\r\n\r\n";
            response += "OK - Server is running";
        }
        // 404 для всех остальных
        else {
            response = "HTTP/1.1 404 Not Found\r\n";
            response += "Content-Type: text/plain\r\n";
            response += "Connection: close\r\n\r\n";
            response += "Not Found";
        }
        
        send(client_fd, response.c_str(), response.length(), 0);
        close(client_fd);
        
        // Уменьшаем счетчик соединений
        {
            std::lock_guard<std::mutex> lock(metrics_mutex);
            current_connections--;
        }
    }
    
    void run() {
        while (running) {
            struct sockaddr_in address;
            int addrlen = sizeof(address);
            int client_fd = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
            if (client_fd >= 0) {
                handleClient(client_fd);
            }
        }
    }
    
public:
    MetricsServer(int port = 8080) {
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) {
            throw std::runtime_error("Failed to create socket");
        }
        
        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        struct sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);
        
        if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
            throw std::runtime_error("Failed to bind socket");
        }
        
        if (listen(server_fd, 10) < 0) {
            throw std::runtime_error("Failed to listen");
        }
        
        server_thread = std::thread(&MetricsServer::run, this);
        std::cout << "✅ Metrics server started on port " << port << std::endl;
    }
    
    ~MetricsServer() {
        running = 0;
        close(server_fd);
        if (server_thread.joinable()) {
            server_thread.join();
        }
    }
};

/**
 * Основная функция
 */
int main() {
    // Настройка обработчиков сигналов
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Запуск сервера метрик
    MetricsServer metrics(8080);
    
    std::cout << "========================================" << std::endl;
    std::cout << "  Fibonacci Calculator with Metrics" << std::endl;
    std::cout << "  Version: 1.0.0" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Endpoints available:" << std::endl;
    std::cout << "  GET  /         - Health check" << std::endl;
    std::cout << "  GET  /health   - Health check" << std::endl;
    std::cout << "  GET  /metrics  - Prometheus metrics" << std::endl;
    std::cout << "  POST /run      - Calculate Fibonacci with POST data" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Example: curl -X POST http://localhost:8080/run -d '10'" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // Бесконечный цикл ожидания сигналов
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "Shutting down..." << std::endl;
    return 0;
}