#include "BF.h"
#include "Kmers.h"
#include "util.h"

#include <jellyfish/file_header.hpp>

BF::BF(const std::string & f, HashPair hp, int nh) :
    filename(f),
    bits(nullptr),
    hashes(hp),
    num_hash(nh)
{ 
}

BF::~BF() {
    if (bits != nullptr) {
        delete bits;
    }
}

// returns true iff the bloom filter contains the given kmer
bool BF::contains(const jellyfish::mer_dna & m) const {
    //std::cout << "TESTING STUFF: " << m.to_str() << std::endl;
    std::string temp = m.to_str();
    jellyfish::mer_dna n = jellyfish::mer_dna(temp);
    n.canonicalize();
    //std::cout << "Canonical version! " << n.to_str() << std::endl;
    uint64_t h0 = hashes.m1.times(n);
    uint64_t h1 = hashes.m2.times(n);

    //DEBUG: std::cout << "size = " << bits->size() << std::endl;
    
    const size_t base = h0 % size();
    const size_t inc = h1 % size();

    for (unsigned long i = 0; i < num_hash; ++i) {
        const size_t pos = (base + i * inc) % size();
        //DEBUG: std::cout << "pos=" << pos << std::endl;
        if ((*this)[pos] == 0) return false;
    }
    return true;
}

// convience function
bool BF::contains(const std::string & str) const {
    //jellyfish::mer_dna temp = jellyfish::mer_dna(str);
    //temp.canonicalize();
    //std::cout << "checking: " << str << "\n";
    //std::cout << "canonical: " << temp.to_str() << "\n";
    return contains(jellyfish::mer_dna(str));
}

uint64_t BF::size() const {
    return bits->size();
}

int BF::operator[](uint64_t pos) const {
    return (*bits)[pos];
}


// read the bit vector and the matrices for the hash functions.
void BF::load() {
    // read the actual bits
    bits = new sdsl::rrr_vector<255>();
    sdsl::load_from_file(*bits, filename);
}

void BF::save() {
    std::cerr << "Saving BF to " << filename << std::endl;
    sdsl::store_to_file(*bits, filename);
}


// create a new RRR bloom filter that is the union of this BF and the given BF.
// Will re-use the hashes from this and both BFs must use exactly the same hash
// (not checked).
BF* BF::union_with(const std::string & new_name, const BF* f2) const {
    assert(bits->size() == f2->size());

    // create an uncompressed version of this bf
    sdsl::bit_vector b(bits->size(), 0);

    std::cerr << "Performing OR... (size " << b.size() << ")" << std::endl;

    // union it with f2
    for (unsigned long i = 0; i < b.size(); i++) {
        b[i] = (*bits)[i] | (*f2->bits)[i];
        if (i % 1000000 == 0) std::cerr << "i=" << i << std::endl;
    }

    // create a new BF wraper for the new BF
    std::cerr << "Building BF object..." << std::endl;
    BF* out = new BF(new_name, hashes, num_hash);

    // "load" the new BF by converting it to a RRR
    std::cerr << "Building RRR vector..." << std::endl;
    out->bits = new sdsl::rrr_vector<255>(b);
    return out;
}

uint64_t BF::similarity(const BF* other) const {
    DIE("not yet implemented");
    return 0;
}

std::tuple<uint64_t, uint64_t> BF::b_similarity(const BF* other) const {
    DIE("not yet implemented");
    return std::make_tuple(0,0);
}


void BF::union_into(const BF* other) {
    DIE("not yet implemented");
}

uint64_t BF::count_ones() const {
    DIE("not yet implemented");
    return 0;
}
/*============================================*/

UncompressedBF::UncompressedBF(const std::string & f, HashPair hp, int nh) :
    BF(f, hp, nh),
    bv(nullptr)
{
}

UncompressedBF::~UncompressedBF() {
    // call to base destructor happens automatically
    if (bv != nullptr) {
        delete bv;
    }
}

void UncompressedBF::load() {
    // read the actual bits
    assert(bv == nullptr);
    bv = new sdsl::bit_vector();
    sdsl::load_from_file(*bv, filename);
}

void UncompressedBF::save() {
    std::cerr << "Saving BF to " << filename << std::endl;
    sdsl::store_to_file(*bv, filename);
}


uint64_t UncompressedBF::size() const {
    return bv->size();
}

int UncompressedBF::operator[](uint64_t pos) const {
    return (*bv)[pos];
}

BF* UncompressedBF::union_with(const std::string & new_name, const BF* f2) const {
    assert(size() == f2->size());
    const UncompressedBF* b = dynamic_cast<const UncompressedBF*>(f2);
    if (b == nullptr) {
        DIE("Can only union two uncompressed BF");
    }
    UncompressedBF* out = new UncompressedBF(new_name, hashes, num_hash);
    out->bv = union_bv_fast(*this->bv, *b->bv);
    return out;
}

void UncompressedBF::union_into(const BF* f2) {
    assert(size() == f2->size());

    const UncompressedBF* b = dynamic_cast<const UncompressedBF*>(f2);
    if (b == nullptr) {
        DIE("Can only union two uncompressed BF");
    }

    uint64_t* b1_data = this->bv->data();
    const uint64_t* b2_data = b->bv->data();

    sdsl::bit_vector::size_type len = size()>>6;
    for (sdsl::bit_vector::size_type p = 0; p < len; ++p) {
        *b1_data = (*b1_data) | (*b2_data++);
        b1_data++;
    }
}

uint64_t UncompressedBF::similarity(const BF* other) const {
    assert(other->size() == size());

    const uint64_t* b1_data = this->bv->data();
    const UncompressedBF* o = dynamic_cast<const UncompressedBF*>(other);
    if (o == nullptr) {
        DIE("Can only compute similarity on same type of BF.");
    }

    const uint64_t* b2_data = o->bv->data();
    
    uint64_t xor_count = 0;
    uint64_t or_count = 0;
    sdsl::bit_vector::size_type len = size()>>6;
    for (sdsl::bit_vector::size_type p = 0; p < len; ++p) {
        xor_count += __builtin_popcountl((*b1_data) ^ (*b2_data));
        or_count += __builtin_popcountl((*b1_data++) | (*b2_data++));
	//count += __builtin_popcountl((*b1_data++) ^ (*b2_data++));
    }
   
    return uint64_t(float(or_count - xor_count) / float(or_count) * size() );
//size() - count;
}



std::tuple<uint64_t, uint64_t> UncompressedBF::b_similarity(const BF* other) const {
    assert(other->size() == size());

    const uint64_t* b1_data = this->bv->data();
    const UncompressedBF* o = dynamic_cast<const UncompressedBF*>(other);
    if (o == nullptr) {
        DIE("Can only compute similarity on same type of BF.");
    }

    const uint64_t* b2_data = o->bv->data();

    uint64_t and_count = 0;
    uint64_t or_count = 0;
    sdsl::bit_vector::size_type len = size()>>6;
    for (sdsl::bit_vector::size_type p = 0; p < len; ++p) {
        and_count += __builtin_popcountl((*b1_data) ^ (*b2_data));
        or_count += __builtin_popcountl((*b1_data++) | (*b2_data++));
        //count += __builtin_popcountl((*b1_data++) ^ (*b2_data++));
    }
    return std::make_tuple(and_count, or_count);
//size() - count;
}


// 00 = 0
// 01 = 1
// 10 = 1
// 11 = 0

int popcount(uint64_t b) {
    int count = 0;
    for (int i = 0; i < 64; i++) {
        if (b % 2) count++;
        b >>= 1;
    }
    return count;
}

// return the # of 1s in the bitvector
uint64_t UncompressedBF::count_ones() const {
    uint64_t* data = bv->data();
    sdsl::bit_vector::size_type len = bv->size()>>6;
    uint64_t count = 0;
    for (sdsl::bit_vector::size_type p = 0; p < len; ++p) {
        int pc = popcount(*data);
        DIE_IF(pc != __builtin_popcountl(*data), "popcountl and us disagree about popcount");
        data++;
        count += pc;
    }

    sdsl::rank_support_v<> rbv(bv);
    DIE_IF(rbv.rank(bv->size()) != count, "SDSL and us disagree about number of 1s");
    return count;
}

// union using 64bit integers
sdsl::bit_vector* union_bv_fast(const sdsl::bit_vector & b1, const sdsl::bit_vector& b2) {
    assert(b1.size() == b2.size());

    sdsl::bit_vector* out = new sdsl::bit_vector(b1.size(), 0);
    uint64_t* out_data = out->data();

    const uint64_t* b1_data = b1.data();
    const uint64_t* b2_data = b2.data();
    sdsl::bit_vector::size_type len = b1.size()>>6;
    for (sdsl::bit_vector::size_type p = 0; p < len; ++p) {
        (*out_data++) = (*b1_data++) | (*b2_data++);
    }
    return out;
}

BF* load_bf_from_file(const std::string & fn, HashPair hp, int nh) {
    if (fn.substr(fn.size()-4) == ".rrr") {
        return new BF(fn, hp, nh);
    } else if (fn.substr(fn.size()-3) == ".bv") {
        return new UncompressedBF(fn, hp, nh);
    } else {
        DIE("unknown bloom filter filetype (make sure extension is .rrr or .bv)");
        return nullptr;
    }
}

