CXX = g++
CXXFLAGS=-O2 -Wall -pedantic -ggdb -DDEBUG -pipe -fvar-tracking-assignments -fno-omit-frame-pointer -ftrack-macro-expansion=2 -fstack-protector-all -fPIC

all: AmdMsrTweaker.o WinRing0.o
	${CXX} AmdMsrTweaker.o WinRing0.o ${CXXFLAGS} -o amdmsrt

AmdMsrTweaker.o: AmdMsrTweaker.cpp mumu.h WinRing0.h
	${CXX} -c AmdMsrTweaker.cpp ${CXXFLAGS} -o AmdMsrTweaker.o

WinRing0.o: WinRing0.cpp WinRing0.h
	${CXX} -c WinRing0.cpp ${CXXFLAGS} -o WinRing0.o

clean:
	rm -f *.o amdmsrt


