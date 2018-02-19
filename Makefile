config: config.o
	g++ -o config -L/usr/local/lib -lftd2xx config.o 

config.o: config.cpp
	g++ -c -I/usr/local/include config.cpp 
