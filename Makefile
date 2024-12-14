
CXX = g++
CXXFLAGS = -Wall -std=c++17 -g

all: server client

server: myserver.cpp
	$(CXX) $(CXXFLAGS) -o twmailer-server myserver.cpp

client: myclient.cpp
	$(CXX) $(CXXFLAGS) -o twmailer-client myclient.cpp

clean:
	rm -f twmailer-server twmailer-client

	
