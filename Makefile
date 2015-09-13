CXX = g++
CXXFLAGS=-O2 -Wall -pedantic -ggdb -DDEBUG -pipe -fvar-tracking-assignments -fno-omit-frame-pointer -ftrack-macro-expansion=2 -fstack-protector-all -fPIC

all: AmdMsrTweaker.o
	${CXX} AmdMsrTweaker.o ${CXXFLAGS} -o amdmsrt

AmdMsrTweaker.o: AmdMsrTweaker.cpp mumu.h
	${CXX} -c AmdMsrTweaker.cpp ${CXXFLAGS} -o AmdMsrTweaker.o

clean:
	rm -f *.o amdmsrt


