CXX=clang++-11
CXXFLAGS=-O2 -pthread -ggdb -Wall -Wextra -std=c++14 -latomic

.PHONY: main clean bench

main: driver

driver: src/driver.cpp
	$(CXX) $(CXXFLAGS) -DREFCOUNT=1 -DSET_PRIORITY=1 -o bin/driver-refcount-pri src/driver.cpp
	$(CXX) $(CXXFLAGS) -DHP=1 -DSET_PRIORITY=1 -o bin/driver-hp-pri src/driver.cpp
	$(CXX) $(CXXFLAGS) -DLOCK=1 -DSET_PRIORITY=1 -o bin/driver-lock-pri src/driver.cpp
	$(CXX) $(CXXFLAGS) -DMPMC=1 -DSET_PRIORITY=1 -o bin/driver-mpmc-pri src/driver.cpp
	$(CXX) $(CXXFLAGS) -DLOCK=1 -o bin/driver-lock src/driver.cpp
	$(CXX) $(CXXFLAGS) -DMPMC=1 -o bin/driver-mpmc src/driver.cpp

clean:
	rm -f bin/* *.dat

bench: driver
	sudo bin/driver-refcount-pri > refcount-pri.dat
	sudo bin/driver-mpmc-pri > mpmc-pri.dat
	gnuplot -e "filenames='refcount-pri.dat mpmc-pri.dat'" gnuplot.script

bench-pri: driver
	sudo bin/driver-lock-pri > lock-pri.dat
	sudo bin/driver-mpmc-pri > mpmc-pri.dat
	sudo bin/driver-lock > lock.dat
	sudo bin/driver-mpmc > mpmc.dat
	gnuplot -e "filenames='lock.dat mpmc.dat lock-pri.dat mpmc-pri.dat'" gnuplot.script
