#include "../src/reticle_art_receipt.hpp"
#include <cassert>
#include <iostream>

int main() {
    const d2vr::ReticleArtReceipt left{10,4,3,1000},right{11,4,3,1010};
    assert(left.matches(10,4,3,1250));
    assert(right.matches(11,4,3,1011));
    assert(!left.matches(11,4,3,1011)); // never use the other eye's frame
    assert(!left.matches(10,5,3,1011));
    assert(!left.matches(10,4,4,1011));
    assert(!left.matches(10,4,3,999));
    assert(!left.matches(10,4,3,1251));
    assert(!d2vr::ReticleArtReceipt{}.matches(0,0,0,0));
    std::cout<<"PASS: kept native reticle art is tied to its eye image and epochs\n";
}
