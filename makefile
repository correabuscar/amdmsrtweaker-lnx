CXX = g++
CXXFLAGS=-O2 -Wall -pedantic -ggdb -DDEBUG -pipe -fvar-tracking-assignments -fno-omit-frame-pointer -ftrack-macro-expansion=2 -fstack-protector-all -fPIC
exe = amdmsrt4myZ575

all: main.o
	${CXX} main.o ${CXXFLAGS} -o ${exe}

main.o: main.cpp mumu.h
	${CXX} -c main.cpp ${CXXFLAGS} -o main.o

clean:
	rm -f *.o ${exe}


