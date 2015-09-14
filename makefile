CXX = g++
CXXFLAGS=-O2 -Wall -pedantic -ggdb -DDEBUG -pipe -fvar-tracking-assignments -fno-omit-frame-pointer -ftrack-macro-expansion=2 -fstack-protector-all -fPIC
exe = amdmsrt4myZ575

all: amdmsrtweaker.o
	${CXX} amdmsrtweaker.o ${CXXFLAGS} -o ${exe}

amdmsrtweaker.o: amdmsrtweaker.cpp mumu.h
	${CXX} -c amdmsrtweaker.cpp ${CXXFLAGS} -o amdmsrtweaker.o

clean:
	rm -f *.o ${exe}


