# Компилятор и флаги
CXX = g++
CXXFLAGS = -Wall -Wextra -O2 -std=c++17 -pthread
LDFLAGS = 

# Имя исполняемого файла
TARGET = fibonacci_metrics

# Исходные файлы
SRCS = fibonacci_metrics.cpp

# Цель по умолчанию
all: $(TARGET)

# Сборка
$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRCS) $(LDFLAGS)
	@echo "Build completed: $(TARGET)"

# Запуск
run: $(TARGET)
	./$(TARGET)

# Очистка
clean:
	rm -f $(TARGET)
	@echo "Clean completed"

# Проверка работы
test: $(TARGET)
	@echo "=== Testing Fibonacci calculator ==="
	@echo -n "Test n=10: "
	@./$(TARGET) 10 2>/dev/null | grep -q "55" && echo "PASS" || echo "FAIL"
	@echo -n "Test n=20: "
	@./$(TARGET) 20 2>/dev/null | grep -q "6765" && echo "PASS" || echo "FAIL"
	@echo "=== Tests completed ==="

.PHONY: all run clean test