@echo off
REM Simple MinGW/Clang build script for Windows.
setlocal

if "%CXX%"=="" set CXX=g++
set FLAGS=-std=c++17 -O3 -Wall -Wextra -I./src
set LIBS=-lws2_32

echo Building market_handler...
%CXX% %FLAGS% src/main.cpp src/udp_receiver.cpp src/message_parser.cpp src/order_book.cpp -o market_handler.exe %LIBS%
if errorlevel 1 exit /b 1

echo Building feed_simulator...
%CXX% %FLAGS% tools/feed_simulator.cpp -o feed_simulator.exe %LIBS%
if errorlevel 1 exit /b 1

echo Building latency_benchmark...
%CXX% %FLAGS% benchmarks/latency_benchmark.cpp -o latency_benchmark.exe %LIBS%
if errorlevel 1 exit /b 1

echo Building tests...
%CXX% %FLAGS% tests/test_ring_buffer.cpp -o test_ring_buffer.exe %LIBS%
if errorlevel 1 exit /b 1
%CXX% %FLAGS% tests/test_parser.cpp -o test_parser.exe %LIBS%
if errorlevel 1 exit /b 1
%CXX% %FLAGS% tests/test_order_book.cpp -o test_order_book.exe %LIBS%
if errorlevel 1 exit /b 1

echo Done. Binaries are in %cd%.
exit /b 0

