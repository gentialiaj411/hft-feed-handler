#include "../src/ring_buffer.h"

#include <cassert>
#include <iostream>

int main() {
    market::SPSCRingBuffer<int, 8> buffer;
    assert(buffer.size() == 0);

    for (int i = 0; i < 7; ++i) {
        assert(buffer.try_push(i));
    }
    assert(!buffer.try_push(99));
    int value = -1;
    for (int i = 0; i < 7; ++i) {
        assert(buffer.try_pop(value));
        assert(value == i);
    }
    assert(!buffer.try_pop(value));

    std::cout << "test_ring_buffer: OK\n";
    return 0;
}
