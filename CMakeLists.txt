cmake_minimum_required(VERSION 3.25)
project(PizzaShell C)

set(CMAKE_C_STANDARD 99)

add_executable(PizzaShell main.c pizza.c)
