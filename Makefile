CXX=clang++

CXXFLAGS=--std=c++11 -Wall

all: build_test

build_test: test.cc mem_handle.hpp
	$(CXX) -o test test.cc -I mem_handle.hpp $(CXXFLAGS)

clean:
	rm test
