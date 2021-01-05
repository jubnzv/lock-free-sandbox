CXX=clang++-11
CXXFLAGS=-O2 -pthread -ggdb -Wall -Wextra -std=c++14 -latomic

.PHONY: main clean bench

main: queue_driver stack_driver

queue_driver: src/queue_driver.cpp
	$(CXX) $(CXXFLAGS) -DLOCK=1 -DSET_PRIORITY=1 -o bin/lock-queue src/queue_driver.cpp
	$(CXX) $(CXXFLAGS) -DMPMC=1 -DSET_PRIORITY=1 -o bin/mpmc-queue src/queue_driver.cpp

stack_driver: src/stack_driver.cpp
	$(CXX) $(CXXFLAGS) -DREFCOUNT=1 -DSET_PRIORITY=1 -o bin/refcount-stack src/stack_driver.cpp
	$(CXX) $(CXXFLAGS) -DHP=1 		-DSET_PRIORITY=1 -o bin/hp-stack	   src/stack_driver.cpp

clean:
	rm -f bin/* *.dat

bench: queue_bench stack_bench

queue_bench:
	sudo bin/lock-queue > lock-queue.dat
	sudo bin/mpmc-queue > mpmc-queue.dat
	gnuplot -e "filenames='lock-queue.dat mpmc-queue.dat'" gnuplot.script
	mv result.png result_queue.png

stack_bench:
	sudo bin/refcount-stack > refcount-stack.dat
	sudo bin/hp-stack > hp-stack.dat
	gnuplot -e "filenames='refcount-stack.dat hp-stack.dat'" gnuplot.script
	mv result.png result_stack.png
