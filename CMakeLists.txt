cmake_minimum_required(VERSION 3.20)
project(StockPredictGUI)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SFML 2.5 COMPONENTS graphics window system REQUIRED)

add_executable(StockPredictGUI main.cpp Predictor.cpp)
target_link_libraries(StockPredictGUI PRIVATE sfml-graphics sfml-window sfml-system)





