#include "cache.h"

#define MAX_PQ_SIZE 32
#define MAX_RQ_SIZE 64
#define MAX_MSHR_SIZE 16
#define MAX_NUM_SET 64
#define MAX_NUM_WAY 8

using namespace std;
#include <bits/stdc++.h>

namespace nMANA {
using namespace std;

// PARAMETERS
uint64_t MANA_TABLE_SINGLE_SETS = 4 * 1024;
uint64_t MANA_TABLE_SINGLE_WAYS = 4;
uint64_t MANA_TABLE_SINGLE_CIRCULAR_HISTORY_SIZE = 1;

uint64_t MANA_TABLE_MULTIPLE_SETS = 1 * 1024;
uint64_t MANA_TABLE_MULTIPLE_WAYS = 4;
uint64_t MANA_TABLE_MULTIPLE_CIRCULAR_HISTORY_SIZE = 4;

int64_t MANA_TABLE_SINGLE_TAG_DOMAIN = 2;
int64_t MANA_TABLE_MULTIPLE_TAG_DOMAIN = log2(MANA_TABLE_SINGLE_SETS) + MANA_TABLE_SINGLE_TAG_DOMAIN - log2(MANA_TABLE_MULTIPLE_SETS);

bool MANA_SUPPORT_MULTIPLE_TABLES = true;

// we have evaluated different regions types
// FLOAT regions, that cover a spatial region around the trigger block are already used in PIF, SHIFT, Confluence, and Shotgun
// However, they need two comparisons to determine a blocks belongs to a region or not. On the contrary, FIXED regions use the pattern
// of the high-order bits to determine a spatial region. It needs a single comparison; however, it slightly downgrades the obtained speedup
enum REGION_TYPE {
	FIXED, FLOATED
};

// We use float regions
//REGION_TYPE ACTUAL_REGION_TYPE = FIXED;
REGION_TYPE ACTUAL_REGION_TYPE = FLOATED;

// The HOBPT configuration. It has 128 set, and 8 way, holding 1024 entries. The set replacement policy is LRU; however, it can be set to FIFO by setting 'HOBPT_uses_LRU = false'
bool USE_HOBPT = true;
uint64_t HOBPT_sets = 128;
uint64_t HOBPT_ways = 8;
bool HOBPT_uses_LRU = true;

// the regions size shows number of blocks encoded in the footprint
const int regionSize = 8;

// the number of spatial regions that are prefetched upon MANA's activation
int theLookahead = 3;

// the length of each stream
int theTrackerSize = 5;

// the length of SRQ
int theSRQSize = 8;

// the number of streams (SABs)
int theStreamCount = 1;


int theBlockSize = 64;
int theBlockOffsetSize = (int) log2(theBlockSize);
uint64_t theBlockMask = ~(theBlockSize - 1);

// Fixed region's configuration. It is not used in this contest, but we have implemented to test the best performing region type
uint64_t FIXED_REGION_SHIFT_OFFSET = 3;
uint64_t FIXED_REGION_MASK = ((1 << FIXED_REGION_SHIFT_OFFSET) - 1);

// Float regions parameters, the number of blocks that are kept before and after the trigger block,
// We found holding 8 blocks ahead of the trigger block is the best performing one; however, different
// policies have negligible difference. Even, (backward = 0 and forward = 4) offers a considerable speedup
// that can be a good design choice since imposes lower storage cost to the design
int64_t FLOATED_BACKWARD_REGION_SIZE = 0;
int64_t FLOATED_FORWARD_REGION_SIZE = 8;
// END PARAMETERS

// The following structure models the entries in the Stream Address Buffer (SAB) and SRQ
// Each entry has the trigger address (theRegionBase) and a footprint (bits)
// StreamEntry is actually a Spatial Region
struct StreamEntry {
	uint64_t theRegionBase;
	bitset<regionSize> bits;

	// Some different constructors
	StreamEntry()
		: theRegionBase(0) {
		bits.reset();
	};
	StreamEntry(uint64_t a)
		: theRegionBase(a) {
		bits.reset();
		if (ACTUAL_REGION_TYPE == FLOATED) {
			//bits.set(0);
		}
		else if (ACTUAL_REGION_TYPE == FIXED) {
			pair<int, bool> index = getIndex(a);
			bits.set(index.first);
		}
	};
	StreamEntry(uint64_t a, uint64_t b)
		: theRegionBase(a)
		, bits(b) {
	};
	StreamEntry(uint64_t a, bitset<regionSize> b)
		: theRegionBase(a)
		, bits(b) {
	};

	// This function determines whether a block address falls in the address space covered by a spatial region or not
	// It gets  'anAddress' to check in the footprint, and sets a given variable 'prefetched' if this address is already
	// observed in this StreamEntry, the return value indicates whether this block is already observed in this StreamEntry or not
	bool inRange(uint64_t anAddress, bool& prefetched) {
		// The implementation when the region type is FLOAT
		if (ACTUAL_REGION_TYPE == FLOATED) {
			// compare the given address with the covered address space boundaries
			if ((theRegionBase - (FLOATED_BACKWARD_REGION_SIZE * theBlockSize)) <= anAddress && anAddress <= (theRegionBase + FLOATED_FORWARD_REGION_SIZE * theBlockSize)) {
				// If it falls in the address space, get the appropriate index in the footprint
				pair<int, bool> index = getIndex(anAddress);
				// check whether  the corresponding bit is set in the footprint, it means that this block is already observed and hence, prefetched
				if (index.second) {
					prefetched = bits[index.first];
				}
				// the trigger block is implicitely held in the footprint, it means that we do not devite a dedicated bit for it in the footprint to avoid unnecessary storage cost, so, it is actually 'prefetched!'
				else {
					prefetched = true;
				}
				return true;
			} else {
				prefetched = false;
				return false;
			}
		}
		// Similar implementations when the region type is FIXED
		else if (ACTUAL_REGION_TYPE == FIXED) {
			if (((anAddress >> LOG2_BLOCK_SIZE) >> FIXED_REGION_SHIFT_OFFSET) == ((theRegionBase >> LOG2_BLOCK_SIZE) >> FIXED_REGION_SHIFT_OFFSET)) {
				prefetched = bits[getIndex(anAddress).first];
				return true;
			}
			else {
				prefetched = false;
				return false;
			}
		}
		assert(false);
		return 0;
	};

	// Sets the index of 'anAddress' in the footprint
	// return value: pair.first: the index in the footprint, pair.second: it is explicitly held or not
	pair<int, bool> getIndex(uint64_t anAddress) const {
		pair<int, bool> index = make_pair(0, true);
		if (ACTUAL_REGION_TYPE == FLOATED) {
			// the index is actually the difference between 'anAddress' and the trigger block in terms of the number of cache blocks
			index.first = (anAddress - theRegionBase) / theBlockSize;

			// ensure that this difference falls in the covered address space by the StreamEntry
			assert(((0 - FLOATED_BACKWARD_REGION_SIZE) <= index.first) && (index.first <= FLOATED_FORWARD_REGION_SIZE));

			// Assign backward blocks to bits at the end of the footprint
			if (index.first < 0) {
				index.first = FLOATED_FORWARD_REGION_SIZE + (-1 * index.first) - 1;
			}
			// The block (trigger+1) is actually held at footprint[0], decrement the index since we implicitly hold the trigger block
			else if (index.first > 0) {
				index.first--;
			}
			// the trigger address is held implicitly, so, set the corresponding flag
			else {
				index.second = false;
			}
			return index;
		}
		// similar implementations for FIXED region type
		else if (ACTUAL_REGION_TYPE == FIXED) {
			index.first = (anAddress >> LOG2_BLOCK_SIZE) & FIXED_REGION_MASK;
			assert(0 <= index.first && (uint)index.first <= FIXED_REGION_MASK);
			return index;
		}

		assert(false);
		return index;
	};

	// extract prefetch candidates using the trigger address and the footprint
	// return value: the block address of the prefetch candidates
	vector<uint64_t> getPrefetchCandidates() {
		vector<uint64_t> prefetch_candidates;
		// implementations for FLOAT region type
		if (ACTUAL_REGION_TYPE == FLOATED) {
			uint64_t prefetch_candidate = theRegionBase;
			//cout << "\n\n\nRB: " << theRegionBase << "\n";
			// the trigger address is definitely a prefetch candidate
			prefetch_candidates.push_back(prefetch_candidate);
			//cout << "Pref 0: " << prefetch_candidate << "\n";
			// traverse in the forward block, prefetch them if their corresponding bit is set in the footprint
			for (uint32_t i = 0; i < FLOATED_FORWARD_REGION_SIZE; i++) {
				prefetch_candidate += 64;
				if (bits[i]) {
					//cout << "Pref " << i+1 << ": " << prefetch_candidate << "\n";
					prefetch_candidates.push_back(prefetch_candidate);
				}
			}

			// discover the backward blocks, prefetch them if their corresponding bit is set in the footprint
			prefetch_candidate = theRegionBase - 64;
			for (uint32_t i = 0; i < FLOATED_BACKWARD_REGION_SIZE; i++) {
				if (bits[FLOATED_FORWARD_REGION_SIZE + i]) {
					//cout << "Pref " << (FLOATED_FORWARD_REGION_SIZE + i + 1) << ": " << prefetch_candidate << "\n";
					prefetch_candidates.push_back(prefetch_candidate);
				}
				prefetch_candidate -= 64;
			}
			return prefetch_candidates;
		}
		// similar implementations for FIXED region type
		else if (ACTUAL_REGION_TYPE == FIXED) {
			uint64_t prefetch_candidate = ((((theRegionBase >> LOG2_BLOCK_SIZE) >> FIXED_REGION_SHIFT_OFFSET) << FIXED_REGION_SHIFT_OFFSET) << LOG2_BLOCK_SIZE);
			for (uint32_t i = 0; i <= FIXED_REGION_MASK; i++) {
				if (bits[i]) {
					prefetch_candidates.push_back(prefetch_candidate);
				}
				prefetch_candidate += 64;
			}
			return prefetch_candidates;
		}
		assert(false);
		return prefetch_candidates;
	}
};

// A forward declaration of struct MANA_TABLE because of the pointer that structu TABLE_PTR has to MANA_TABLE
struct MANA_TABLE;

// A pointer to MANA_TABLE, this records that where a specific entry is located, in which MANA_TABLE, in which set, and in which way
// For simplicity, we assume that each record has the pointer to the corresponding MANA_TABLE. However, we assume a single bit storage cost for the entry 'MANA_table' since it just determines in which table the record is stored. Our implementation has at most two tables: MANA_Table (single), MANA_Table (multiple). The pointer is later used to access the MANA_TABLE object to read or manipulate its content
struct TABLE_PTR
{
	uint64_t set = 0;
	uint64_t way = 0;
	MANA_TABLE * MANA_table = NULL;

	TABLE_PTR(uint64_t s = ULLONG_MAX, uint64_t w = ULLONG_MAX, MANA_TABLE * mt = NULL) {
		set = s;
		way = w;
		MANA_table = mt;
	}
};

// The structure of a stream address buffer (SAB), it has a list of StreamEntries and a pointer to the table entry the last StreamEntry is fetched from.
struct Stream {
	TABLE_PTR theTailTablePos;
	list<StreamEntry> theStreamEntries;

	Stream() {};
};

// This structure is used to fetch StreamEntries from MANA_TABLE, it has a pointer to the stream that triggers the prefetches
// Moreover, it has a TABLE_PTR to be able to chase the spatial regions by having the last fetched spatial region.
struct Ptr {
	void* theStream;
	TABLE_PTR theTablePos;

	Ptr() {
	};

	Ptr(void* aStreamIdx, const TABLE_PTR aTablePos)
		: theStream(aStreamIdx)
		, theTablePos(aTablePos)
	{ };
};

// This structure holds an object of PTR along with 'theLength' that stores the number of spatial regions that should be chased to ensure a predefined lookahead
struct Range {
	Ptr thePtr;
	int theLength = 0;

	Range() {};

	Range(void* aStream, const TABLE_PTR aTablePos, int aLength)
		: thePtr(aStream, aTablePos)
		, theLength(aLength)
	{ };
};

// The following structure, as its name suggests, tracks the streams. It is responsible for the following tasks:
// a) Does a new arriving block belong to a StreamEntry (or equivalently, spatial region), in the SABs?
// b) If the answer to the previous question is yes, prefetch as required spatial regions to ensure the predefined lookahead and push them to the stream that has triggered their prefetch.
// c) Otherwise, evict the LRU stream, and allocate a new stream
struct StreamTracker {
	// local parameters, set according to the variables at the beginning of the MANA's namespace
	int theStreamCount;
	int theTrackerSize;
	int theLookahead;

	list<Stream*> theStreams;

	// constructor
	StreamTracker(int sc, int ts, int la) {
		theStreamCount = sc;
		theTrackerSize = ts;
		theLookahead = la;

		// fill in the streams with dummy entries
		for (int i = 0; i < theStreamCount; i++) {
			theStreams.push_back(new Stream);
		}
	};

	// lookup 'theAddress' in the all tracked streams, update 'aRange' according to the place of a possible match in the stream
	bool lookup(uint64_t theAddress, bool& prefetched, Range & aRange) {
		prefetched = false;

		// iterate over the streams
		for (list<Stream*>::iterator s = theStreams.begin(); s != theStreams.end(); s++) {
			Stream* aStream = *s;
			int n = 0;

			// iterator over the spatial regions in the stream
			for (list<StreamEntry>::iterator a = aStream->theStreamEntries.begin(); a != aStream->theStreamEntries.end(); a++, n++) {
				// check if the address falls in the address space covered by a spatial region
				if (a->inRange(theAddress, prefetched)) {
					if (!prefetched) continue;

					// make the matching stream MRU
					theStreams.splice(theStreams.begin(), theStreams, s);

					// calculate the prefetching lookahead, the matching spatial region must see 'theLookahead' spatial regions ahead of it
					// the matching spatial region is in position 'n', the number of entries ahead of it is: 'theTrackerSize - n',
					// if this value is lower than 'theLookahead', prefetch 'theLookahead - (theTrackerSize - n)' spatial regions to provide the lookahead
					if ((theTrackerSize - n) < theLookahead) {
						aRange = Range(aStream, aStream->theTailTablePos, theLookahead - (theTrackerSize - n));
						return true;
					}

					// if the sufficient lookahead is already provided, the prefetching lookahead is '0'
					aRange = Range(aStream, aStream->theTailTablePos, 0);
					return true;
				}
			}
		}
		// if 'theAddress' is missed in SABs, return false
		return false;
	};

	// when a new spatial region is prefetched, insert it into the corresponding stream
	bool push_back(Ptr& aPtr, const StreamEntry& entry) {
		// find the stream according to the pointer in 'aPtr'
		Stream* aStream = (Stream*)aPtr.theStream;

		// Remove the head of the queue
		aStream->theStreamEntries.pop_front();

		// Push the new patial region to the queue
		aStream->theStreamEntries.push_back(entry);
		return true;
	};


	// If SABs do not cover an observed spatial region, allocate a new one, and set its pointer to the MANA_TABLE entry the observed spatial region is coming from
	Range allocate(TABLE_PTR aTablePos) {
		// Evict the LRU stream, it is at the back of the queue
		Stream* aStream = theStreams.back();
		aStream->theStreamEntries.clear();

		// set the pointer
		aStream->theTailTablePos = aTablePos;

		// file the stream with dummy spatial regions, they will be correctly shortly using the push_back method
		StreamEntry aDummyEnt;
		for (int i = 0; i < theTrackerSize; ++i) {
			aStream->theStreamEntries.push_back(aDummyEnt);
		}

		// make this stream MRU
		theStreams.splice(theStreams.begin(), theStreams, --theStreams.end());
		return Range(aStream, aTablePos, theLookahead);
	};
};

// a class that model circular history, circular history is used for MANA_TABLE (multiple)
// MANA_TABLE (single) actually does not need a circular history since it records a single pointer to the successor spatial region
// However, to avoid having multiple interfaces for MANA_TABLE's, we implement MANA_TABLE (single) in a way that it has a single entry circular history
class CIRCULAR_HISTORY {
public:
	uint64_t size; // the size of history, we don't consider a storage cost for it, ...
	// ... it is used just to ease some logical operations without the need to have the history size using methods like history.size()
	//
	uint64_t CH_index; // pointer to the element that a new entry should be placed
	vector<TABLE_PTR> history; // history

	// constructor
	CIRCULAR_HISTORY(uint64_t size = 1) {
		this->size = size;
		history.resize(size);
		CH_index = 0;
	}

	// adds a new entry to the history
	void add_history(TABLE_PTR entry) {
		history[CH_index] = entry;
		CH_index = (CH_index + 1) % size;
	}


	// Rewrite the content of the last inserted entry
	void override_history(TABLE_PTR entry) {
		CH_index = rem(CH_index - 1);
		history[CH_index] = entry;
		CH_index = rem(CH_index + 1);
	}

	// a helper function to calculate the CH_index when it overflows or underflows
	uint64_t rem(int64_t index) {
		uint64_t ret_val = ((index + size) % size);
		return ret_val;
	}



	// consults history to make a prediction,
	// consider the following history: (a, b, c, d, a, (empty))
	// we want to predict what is the next entry that will be inserted into the history
	// the last entry that is inserted into the history is 'a'
	// this function find the second last appereance of 'a' and return the entry that
	// is inserted after it which is 'b'
	vector<TABLE_PTR> get_prediction() {
		uint64_t index = rem(CH_index - 1);
		TABLE_PTR lastEntry = history[index];
		vector<TABLE_PTR> ret_history;
		for (uint32_t i = 0; i < size; i++) {
			index = rem(CH_index - 2 - i);
			TABLE_PTR candidate = history[index];
			if (candidate.set == lastEntry.set && candidate.way == lastEntry.way && candidate.MANA_table == lastEntry.MANA_table) {
				ret_history.push_back(history[rem(CH_index - 2 - i + 1)]);
				return ret_history;
			}
		}

		// if a match is not found, return a dummy response!
		ret_history.clear();
		ret_history.push_back(TABLE_PTR(ULLONG_MAX, ULLONG_MAX, NULL));
		return ret_history;
	}


	// returns whether an entry is in the history or not
	bool find(TABLE_PTR entry) {
		for (uint32_t i = 0; i < size; i++) {
			if (history[i].set == entry.set && history[i].way == entry.way && history[i].MANA_table == entry.MANA_table) {
				return true;
			}
		}
		return false;
	}

	// returns whether the circular history has a valid entry
	bool active() {
		for (uint32_t i = 0; i < size; i++) {
			if (history[i].MANA_table != NULL) {
				return true;
			}
		}
		return false;
	}

	void print_history() {
		cout << "CH_index: " << CH_index << "\n";
		for (uint32_t i = 0; i < size; i++) {
			if (history[i].MANA_table == NULL) {
				break;
			}
			cout << "CH " << i << ": " << history[i].set << " " << history[i].way << " " << history[i].MANA_table << "\n";
		}
	}

	vector<TABLE_PTR> get_history() {
		return history;
	}


	// when we send a spatial region from MANA_TABLE (single) to MANA_TABLE (multiple), we should make their circular history consistent if their history sizes are different
	void change_history_size(int new_history_size) {
		// change the history size
		this->size = new_history_size;

		// make a copy of its content
		vector<TABLE_PTR > tmp_history = history;

		// clear the old and create a new history
		history.clear();
		history.resize(size);
		for (uint32_t i = 0; i < history.size(); i++) {
			history[i] = TABLE_PTR();
		}

		// copy the content to the new history
		for (uint32_t i = 0; i < tmp_history.size(); i++) {
			history[i % size] = tmp_history[(CH_index + i) % tmp_history.size()];
		}

		// update the tail pointer
		CH_index = tmp_history.size() % size;
	}

	uint64_t get_size() {
		return size;
	}

	void update(CIRCULAR_HISTORY tmp) {
		this->size = tmp.size;
		this->CH_index = tmp.CH_index;
		this->history = tmp.history;
	}
};


// A structuer to hold the observed high-order bit-patterns (HOBP), it has a set-associative structure.
// We have implemented both LRU and FIFO policies.
struct HOBP_HOLDER {
	// The structure of each table entry
	struct HOBP_HOLDER_SET
	{
		vector<uint64_t> WAYS; // the ways, each holds a HOBP
		uint64_t tail = 0; // the tail pointer of the way, if we use the FIFO replacement policy
		vector<uint64_t> LRU_order; // the LRU order of the ways, if we use LRU policy
	};

	vector< HOBP_HOLDER_SET > table; // the actual table

	uint64_t num_of_sets;
	uint64_t num_of_ways;
	uint64_t num_of_sets_bits;
	uint64_t set_mask;

	// constructor
	HOBP_HOLDER(uint64_t s, uint64_t w) {
		num_of_sets = s;
		num_of_ways = w;
		num_of_sets_bits = log2(num_of_sets);
		set_mask = ((1 << num_of_sets_bits) - 1);
		table.resize(num_of_sets);
		for (uint64_t i = 0; i < num_of_sets; i++) {
			table[i].WAYS.resize(num_of_ways);
			for (uint64_t j = 0; j < num_of_ways; j++) {
				table[i].LRU_order.push_back(j);
			}
		}
	}

	// this function gets a pointer to a table entry and return the HOBP stored in that entry
	uint64_t get(pair<uint64_t, uint64_t> HOBP_index) {
		return ((table[HOBP_index.first].WAYS[HOBP_index.second] << num_of_sets_bits) + HOBP_index.first);
	}

	// this function determines where a requested pattern is located, the return value is the pair of (set, way) of the stored pattern in the table
	pair<uint64_t, uint64_t> find(uint64_t pattern) {
		pair<uint64_t, uint64_t> ret_val;

		uint64_t set_number = pattern & set_mask;
		uint64_t tag = pattern >> num_of_sets_bits;

		bool hit = false;
		// iterate over the table ways
		for (uint64_t i = 0; i < num_of_ways; i++) {

			// use this block if replacement policy is LRU
			if (HOBPT_uses_LRU) {
				// compare the tags
				if (table[set_number].WAYS[i] == tag) {
					hit = true;
					ret_val = make_pair(set_number, i);

					// update the LRU_order of the replacement policy
					for (uint64_t j = num_of_ways - 1; j >= 0; j--) {
						if (table[set_number].LRU_order[j] == i) {
							table[set_number].LRU_order.erase(table[set_number].LRU_order.begin() + j);
							table[set_number].LRU_order.push_back(i);
							break;
						}
					}
				}
			}
			// use this block if the replacement policy is FIFO
			else {
				if (table[set_number].WAYS[i] == tag) {
					hit = true;
					ret_val = make_pair(set_number, i);
					break;
				}
			}
		}

		// if the observed pattern is not found, insert it into the table
		if (!hit) {
			ret_val = insert(pattern);
		}

		return ret_val;
	}

	// This function inserts an observed pattern into the table and returns its position
	pair<uint64_t, uint64_t> insert(uint64_t pattern) {
		pair<uint64_t, uint64_t> ret_val;

		uint64_t set_number = pattern & set_mask;
		uint64_t tag = pattern >> num_of_sets_bits;

		// use this block if the replacement policy is LRU
		if (HOBPT_uses_LRU) {
			// the LRU way is at the head of the LRU_order, access to it in O(1)
			uint64_t LRU_idx = table[set_number].LRU_order[0];

			// update the LRU way with the new pattern
			table[set_number].WAYS[LRU_idx] = tag;
			ret_val = make_pair(set_number, LRU_idx);

			// update the LRU order, makes the new entry MRU
			table[set_number].LRU_order.erase(table[set_number].LRU_order.begin());
			table[set_number].LRU_order.push_back(LRU_idx);
		}
		// use this block if the replacement policy is FIFO
		else {
			// the tail pointer shows where the new entry should be inserted
			table[set_number].WAYS[table[set_number].tail] = tag;
			ret_val = make_pair(set_number, table[set_number].tail);

			// increment the tail pointer
			table[set_number].tail++;
			// bring the tail to '0' position if the tail pointer overflows
			if (table[set_number].tail >= num_of_ways) {
				table[set_number].tail = 0;
			}
		}
		return ret_val;
	}
};

// instantiate the HOBPT
HOBP_HOLDER HOBPT = HOBP_HOLDER(HOBPT_sets, HOBPT_ways);

// structure represents each MANA_TABLE's entry
struct MANA_entry {
	uint64_t partial_tag = ULLONG_MAX; // the partial tag
	pair<uint64_t, uint64_t> HOBP_index = make_pair(0, 0); // the pointer to the HOBPT
	bitset<regionSize> footprint; // footprint!
	CIRCULAR_HISTORY next_ptr_ch; // pointer to the successor spatial region
};

// structure that represents MANA_TABLE
struct MANA_TABLE
{
	uint64_t num_of_sets;
	uint64_t num_of_ways;
	uint64_t num_of_sets_bits;
	uint64_t set_mask;

	vector< vector < MANA_entry > > table; // the actual table
	vector< vector < uint64_t > > LRU_order; // the LRU_order of each set

	TABLE_PTR lastInserted; // is used to find the last inserted entry to update its successor pointer when a new entry is added to the table
	TABLE_PTR secondLastInserted; // is used to find the second last inserted entry to update its successor pointer when the last inserted entry is moved from MANA_TABLE (single) to MANA_TABLE (multiple)

	uint64_t pTagDomain; // the domain in which the partial tag gets its value
	int64_t PARTIAL_TAG_SHIFT_WIDTH; // the number of bits in the address space devoted to the partial tag

	uint32_t circular_history_size; // table knows it circualr history size
	MANA_TABLE * other_MANA_table; // each table has a pointer to the another table to talk to each other
	bool has_second_table; // each table know whether there is another MANA_TABLE

	// constructor
	MANA_TABLE(uint32_t s = 1024, uint32_t w = 4, uint32_t chs = 1, bool hst = 0, uint64_t td = 2) {
		num_of_ways = w;
		num_of_sets = s;
		num_of_sets_bits = log2(num_of_sets);
		set_mask = ((1 << num_of_sets_bits) - 1);

		circular_history_size = chs;
		has_second_table = hst;

		// create and fill the table with default values
		table.resize(num_of_sets);
		LRU_order.resize(num_of_sets);
		for (uint32_t i = 0; i < num_of_sets; i++) {
			table[i].resize(num_of_ways);
			for (uint32_t j = 0; j < num_of_ways; j++) {
				table[i][j].next_ptr_ch = CIRCULAR_HISTORY(circular_history_size);
				LRU_order[i].push_back(j);
			}
		}

		// these pointers are set to have dummy values
		lastInserted = TABLE_PTR(ULLONG_MAX, ULLONG_MAX, NULL);
		secondLastInserted = TABLE_PTR(ULLONG_MAX, ULLONG_MAX, NULL);

		// set the pTagDomain according to the number of bits devoted to the partial tag
		PARTIAL_TAG_SHIFT_WIDTH = td;
		if (PARTIAL_TAG_SHIFT_WIDTH >= 0) {
			pTagDomain = pow(2, PARTIAL_TAG_SHIFT_WIDTH);
		}
		else {
			pTagDomain = pow(2, (-1 * PARTIAL_TAG_SHIFT_WIDTH));
		}
	}

	// This function records the spatial region evicted from SRQ
	bool record(StreamEntry& e) {
		if (!(e.theRegionBase & ~63)) {
			return false;
		}

		uint64_t address = e.theRegionBase;
		uint64_t block = address >> LOG2_BLOCK_SIZE;

		uint64_t set = block & set_mask;
		uint64_t tag = (block >> num_of_sets_bits);

		bool found = false;
		uint32_t found_idx = 0;
		// iterate over the ways to find a probable match
		for (uint32_t i = 0; i < table[set].size(); i++) {
			uint64_t way_tag;

			// MANA_TABLE does not hold the tag to reduce the storage cost, it constructs the tag using the HOBPT
			// it gets the HOBP using the pointer to HOBPT that is 'HOBP_index'
			//
			// Depending on the number of sets MANA_TABLE (single) and (multiple) have and the number of bits devoted to the partial tag,
			// two separate methods required to extract the actual tag using HOBP, it depends on the value of 'PARTIAL_TAG_SHIFT_WIDTH'
			if (PARTIAL_TAG_SHIFT_WIDTH >= 0) {
				way_tag = (HOBPT.get(table[set][i].HOBP_index) << PARTIAL_TAG_SHIFT_WIDTH) + table[set][i].partial_tag;
			}
			else {
				way_tag = (HOBPT.get(table[set][i].HOBP_index) >> (-1 * PARTIAL_TAG_SHIFT_WIDTH));
			}

			// if the tags match, it is actualy a MANA_TABLE hit!
			if (way_tag == tag) {
				found = true;
				found_idx = i;
				break;
			}
		}

		// if it is a MANA_TABLE hit, update the already recorded entry with the latest footprint
		if (found) {
			table[set][found_idx].footprint = e.bits;

			// update the LRU order of the replacement policy
			for (uint64_t j = num_of_ways - 1; j >= 0; j--) {
				if (LRU_order[set][j] == found_idx) {
					LRU_order[set].erase(LRU_order[set].begin() + j);
					LRU_order[set].push_back(found_idx);
					break;
				}
			}

			// look at the lastInserted entry, if it has a successor that is not the currently inserted spatial region,
			// the lastInserted entry has multiple successors and should be sent to MANA_TABLE (multiple)
			//
			// This process start with ensuring that the lastInserted entry is a valid one
			if (lastInserted.MANA_table != NULL) {
				// second, the lastInserted entry's MANA_TABLE should have the second table
				if (lastInserted.MANA_table->has_second_table) {
					// Moreover, the current successor should miss in lastInserted entry's successors
					if (!lastInserted.MANA_table->table[lastInserted.set][lastInserted.way].next_ptr_ch.find(TABLE_PTR(set, found_idx, this))) {
						// In addition, if the current successor misses in the lastInserted entry's successors, make sure that its successor pointers are not empty
						if (lastInserted.MANA_table->table[lastInserted.set][lastInserted.way].next_ptr_ch.active()) {
							// If all requirements are satisfied, move the lastInserted entry to MANA_TABLE (multiple)
							lastInserted.MANA_table->other_MANA_table->record(lastInserted, TABLE_PTR(set, found_idx, this));
							return true;
						}
					}
				}

				// if MANA prefetcher does not support multiple successors, simply update its successor with the lastest one
				lastInserted.MANA_table->table[lastInserted.set][lastInserted.way].next_ptr_ch.add_history(TABLE_PTR(set, found_idx, this));
			}

			// update lastInserted and secondLastInserted pointers
			secondLastInserted = lastInserted;
			lastInserted = TABLE_PTR(set, found_idx, this);

			// inform the other MANA_TABLE to have the latest news
			if (other_MANA_table != NULL) {
				other_MANA_table->lastInserted = lastInserted;
				other_MANA_table->secondLastInserted = secondLastInserted;
			}
			return true;
		}
		// the new entry is missed in MANA_TABLE! insert it
		else {
			// find the LRU way, it is at the head of LRU_order, access it in O(1)
			uint64_t LRU_idx = LRU_order[set][0];
			// make it MRU
			LRU_order[set].erase(LRU_order[set].begin());
			LRU_order[set].push_back(LRU_idx);

			// record the new entry in the LRU way
			table[set][LRU_idx].partial_tag = tag % pTagDomain;
			table[set][LRU_idx].footprint = e.bits;

			// set HOBP_index by looking up the pattern in the HOBPT
			if (PARTIAL_TAG_SHIFT_WIDTH >= 0) {
				table[set][LRU_idx].HOBP_index = HOBPT.find(tag >> PARTIAL_TAG_SHIFT_WIDTH);
			}
			else {
				uint64_t ptrn = (tag << (-1 * PARTIAL_TAG_SHIFT_WIDTH)) + (set >> (num_of_sets_bits - (-1 * PARTIAL_TAG_SHIFT_WIDTH)));
				table[set][LRU_idx].HOBP_index = HOBPT.find(ptrn);
			}

			// same as above, decide whether the lastInserted entry should be sent to MANA_TABLE (multiple) or not
			if (lastInserted.MANA_table != NULL) {
				if (lastInserted.MANA_table->has_second_table) {
					if (!lastInserted.MANA_table->table[lastInserted.set][lastInserted.way].next_ptr_ch.find(TABLE_PTR(set, LRU_idx, this))) {
						if (lastInserted.MANA_table->table[lastInserted.set][lastInserted.way].next_ptr_ch.active()) {
							lastInserted.MANA_table->other_MANA_table->record(lastInserted, TABLE_PTR(set, LRU_idx, this));
							return false;
						}
					}
				}
				lastInserted.MANA_table->table[lastInserted.set][lastInserted.way].next_ptr_ch.add_history(TABLE_PTR(set, LRU_idx, this));
			}

			// update lastInserted and secondLastInserted pointers
			secondLastInserted = lastInserted;
			lastInserted = TABLE_PTR(set, LRU_idx, this);

			// update the other MANA_TABLE with the latest news
			if (other_MANA_table != NULL) {
				other_MANA_table->lastInserted = lastInserted;
				other_MANA_table->secondLastInserted = secondLastInserted;
			}
			return false;
		}
	};

	// this function moves an entry from MANA_TABLE single to MANA_TABLE multiple
	bool record(TABLE_PTR li, TABLE_PTR nx) {
		uint64_t block;
		// reconstruct the blocks address of lastInserted entry
		if (li.MANA_table->PARTIAL_TAG_SHIFT_WIDTH >= 0) {
			block = ((((HOBPT.get(li.MANA_table->table[li.set][li.way].HOBP_index) << li.MANA_table->PARTIAL_TAG_SHIFT_WIDTH) + li.MANA_table->table[li.set][li.way].partial_tag) << li.MANA_table->num_of_sets_bits) + li.set);
		}
		else {
			block = (((HOBPT.get(li.MANA_table->table[li.set][li.way].HOBP_index) >> (-1 * li.MANA_table->PARTIAL_TAG_SHIFT_WIDTH)) << li.MANA_table->num_of_sets_bits) + li.set);
		}

		uint64_t set = block & set_mask;
		uint64_t tag = (block >> num_of_sets_bits);


		bool found = false;
		// iterate over the ways to find a probable match
		for (uint32_t i = 0; i < table[set].size(); i++) {
			uint64_t way_tag;
			if (PARTIAL_TAG_SHIFT_WIDTH >= 0) {
				way_tag = (HOBPT.get(table[set][i].HOBP_index) << PARTIAL_TAG_SHIFT_WIDTH) + table[set][i].partial_tag;
			}
			else {
				way_tag = (HOBPT.get(table[set][i].HOBP_index) >> (-1 * PARTIAL_TAG_SHIFT_WIDTH));
			}
			// compare tags here
			if (way_tag == tag) {
				found = true;
			}
		}

		// actually we don't expect to have a match since there should be a single instance of each spatial region in the MANA_TABLEs
		if (found) {
		}
		else {
			// find the LRU way, it is at the head of the LRU_order
			uint64_t LRU_idx = LRU_order[set][0];

			// make it MRU
			LRU_order[set].erase(LRU_order[set].begin());
			LRU_order[set].push_back(LRU_idx);

			// update the content of the entry
			table[set][LRU_idx].HOBP_index = li.MANA_table->table[li.set][li.way].HOBP_index;
			table[set][LRU_idx].partial_tag = tag % pTagDomain;
			table[set][LRU_idx].footprint = li.MANA_table->table[li.set][li.way].footprint;
			table[set][LRU_idx].next_ptr_ch.update(li.MANA_table->table[li.set][li.way].next_ptr_ch);

			// delete the instance in the MANA_TABLE (single)
			li.MANA_table->table[li.set][li.way] = MANA_entry();

			// lastInserted is actually in the MANA_TABLE (multiple), so update it
			lastInserted = TABLE_PTR(set, LRU_idx, this);

			// fix the secondLastInserted's successor to point to the new position of the lastInserted
			secondLastInserted.MANA_table->table[secondLastInserted.set][secondLastInserted.way].next_ptr_ch.override_history(lastInserted);

			// set lastInserted's successor pointer to the new inserted entry
			lastInserted.MANA_table->table[lastInserted.set][lastInserted.way].next_ptr_ch.change_history_size(circular_history_size);
			lastInserted.MANA_table->table[lastInserted.set][lastInserted.way].next_ptr_ch.add_history(nx);

			// update lastInserted and secondLastInserted
			secondLastInserted = lastInserted;
			lastInserted = nx;

			// inform the other MANA_TABLE with the latest news
			if (other_MANA_table) {
				other_MANA_table->lastInserted = lastInserted;
				other_MANA_table->secondLastInserted = secondLastInserted;
			}

			return false;
		}
		return false;
	};

	// get the position of a trigger address in the MANA_TABLE
	TABLE_PTR getPtr(uint64_t address) {
		uint64_t block = address >> LOG2_BLOCK_SIZE;

		uint64_t set = block & set_mask;
		uint64_t tag = (block >> num_of_sets_bits);

		uint64_t found_way = 0;
		bool found = false;
		// iterate over the ways to find a match
		for (uint32_t i = 0; i < table[set].size(); i++) {
			uint64_t way_tag;
			// construct a tag using the HOBP
			if (PARTIAL_TAG_SHIFT_WIDTH >= 0) {
				way_tag = (HOBPT.get(table[set][i].HOBP_index) << PARTIAL_TAG_SHIFT_WIDTH) + table[set][i].partial_tag;
			}
			else {
				way_tag = (HOBPT.get(table[set][i].HOBP_index) >> (-1 * PARTIAL_TAG_SHIFT_WIDTH));
			}
			// compare the tags
			if (way_tag == tag) {
				found = true;
				found_way = i;
				break;
			}
		}

		if (found) {
			// update the matching way to the MRU position
			for (uint64_t j = num_of_ways - 1; j >= 0; j--) {
				if (LRU_order[set][j] == found_way) {
					LRU_order[set].erase(LRU_order[set].begin() + j);
					LRU_order[set].push_back(found_way);
					break;
				}
			}
			return TABLE_PTR(set, found_way, this);
		}
		// if the entry does not exist, return a dummy reply
		return TABLE_PTR(ULLONG_MAX, ULLONG_MAX, NULL);
	};

	// fast lookup, checks whether a trigger address exists in the MANA_TABLE
	// without updating it to the MRU position
	bool flookup(uint64_t address) {
		uint64_t block = address >> LOG2_BLOCK_SIZE;

		uint64_t set = block & set_mask;
		uint64_t tag = (block >> num_of_sets_bits);

		// iterate over the ways
		for (uint32_t i = 0; i < table[set].size(); i++) {
			uint64_t way_tag;
			// construct the tag using the HOBP
			if (PARTIAL_TAG_SHIFT_WIDTH >= 0) {
				way_tag = (HOBPT.get(table[set][i].HOBP_index) << PARTIAL_TAG_SHIFT_WIDTH) + table[set][i].partial_tag;
			}
			else {
				way_tag = (HOBPT.get(table[set][i].HOBP_index) >> (-1 * PARTIAL_TAG_SHIFT_WIDTH));
			}
			// compare the tags
			if (way_tag == tag) {
				return true;
			}
		}
		return false;
	};

	// prints the content of the MANA_TABLE
	void printMANATable() {
		for (uint32_t i = 0; i < num_of_sets; i++) {
			for (uint32_t j = 0; j < num_of_ways; j++) {
				printMANATableEntry(TABLE_PTR(i, j, this));
			}
		}
	}

	// print a single entry of the MANA_TABLE indicated with a pointer to a table entry
	void printMANATableEntry(TABLE_PTR ptr) {
		if (ptr.MANA_table == NULL) {
			cout << "entry is NULL\n";
			return;
		}

		cout << "circular history size of table: " << ptr.MANA_table->circular_history_size << "\n";
		cout << "Set: " << ptr.set << "\n";
		cout << "Way: " << ptr.way << "\n";
		uint64_t address;
		if (PARTIAL_TAG_SHIFT_WIDTH >= 0) {
			address = ((((HOBPT.get(ptr.MANA_table->table[ptr.set][ptr.way].HOBP_index) << PARTIAL_TAG_SHIFT_WIDTH) + ptr.MANA_table->table[ptr.set][ptr.way].partial_tag) << ptr.MANA_table->num_of_sets_bits) + ptr.set) << LOG2_BLOCK_SIZE;
		}
		else {
			address = (((HOBPT.get(ptr.MANA_table->table[ptr.set][ptr.way].HOBP_index) >> (-1 * PARTIAL_TAG_SHIFT_WIDTH)) << ptr.MANA_table->num_of_sets_bits) + ptr.set) << LOG2_BLOCK_SIZE;
		}
		cout << "Address: " << address << "\n";
		cout << "partial_tag: " << ptr.MANA_table->table[ptr.set][ptr.way].partial_tag << "\n";
		cout << "HOBP_index: " << ptr.MANA_table->table[ptr.set][ptr.way].HOBP_index.first << "\t" << ptr.MANA_table->table[ptr.set][ptr.way].HOBP_index.second << "\n";
		cout << "footprint: " << ptr.MANA_table->table[ptr.set][ptr.way].footprint.to_ulong() << "\n";
		ptr.MANA_table->table[ptr.set][ptr.way].next_ptr_ch.print_history();
	}

	// a helper function
	void set_other_MANA_table(MANA_TABLE * ptr) {
		other_MANA_table = ptr;
	}
};

// This class manages the interface the MANA prefetcher needs to work with two MANA_TABLEs
struct MANA_TABLES {
	// The pointer to two MANA_TABLEs (single & multiple)
	MANA_TABLE * MANA_table_single;
	MANA_TABLE * MANA_table_multiple;

	// a flag that shows whether MANA prefetcher uses two tables or not
	bool support_multiple_tables;

	// constructor
	MANA_TABLES(uint32_t ss = 1024, uint32_t ws = 4, uint32_t chs = 1, int64_t tds = 2, uint32_t sm = 1024, uint32_t wm = 4, uint32_t chm = 1, int64_t tdm = 2, bool smt = false) {
		support_multiple_tables = smt;

		// instantiate MANA_TABLE (single)
		MANA_table_single = new MANA_TABLE(ss, ws, chs, smt, tds);

		// if MANA uses two tables, instantiate the MANA_TABLE (multiple)
		// and set the correponding pointer of each object to the other table
		if (support_multiple_tables) {
			MANA_table_multiple = new MANA_TABLE(sm, wm, chm, false, tdm);
			MANA_table_single->set_other_MANA_table(MANA_table_multiple);
			MANA_table_multiple->set_other_MANA_table(MANA_table_single);
		}
		else {
			MANA_table_single->set_other_MANA_table(NULL);
		}
	}

	// destructor, releases the pointers
	~MANA_TABLES() {
		delete MANA_table_single;
		delete MANA_table_multiple;
	}

	// reads the spatial region that 'aRange' points to it and fills the 'entry' with the content of the pointed location
	// return a boolean, whether the pointer points to a valid entry or not
	bool read(Range& aRange, StreamEntry& entry) {
		// extract the pointer from the 'aRange' object
		TABLE_PTR tail = ((Stream*)aRange.thePtr.theStream)->theTailTablePos;
		// check that pointer to be valid
		if (tail.set != ULLONG_MAX && tail.way != ULLONG_MAX) {
			// construct the block address using HOBP, partial tag, and set number
			uint64_t theRegionBase;
			if (tail.MANA_table->PARTIAL_TAG_SHIFT_WIDTH >= 0) {
				theRegionBase = ((((HOBPT.get(tail.MANA_table->table[tail.set][tail.way].HOBP_index) << tail.MANA_table->PARTIAL_TAG_SHIFT_WIDTH) + tail.MANA_table->table[tail.set][tail.way].partial_tag) << tail.MANA_table->num_of_sets_bits) + tail.set) << LOG2_BLOCK_SIZE;
			}
			else {
				theRegionBase = (((HOBPT.get(tail.MANA_table->table[tail.set][tail.way].HOBP_index) >> (-1 * tail.MANA_table->PARTIAL_TAG_SHIFT_WIDTH)) << tail.MANA_table->num_of_sets_bits) + tail.set) << LOG2_BLOCK_SIZE;
			}
			// return the spatial region, its trigger address and footprint are ready to be used
			entry = StreamEntry(theRegionBase, tail.MANA_table->table[tail.set][tail.way].footprint);
			return true;
		}
		// no valid entry is given to be read
		return false;
	};

	// chase the pointer
	void incrementPointer(Range& aRange) {
		// get the pointer of the 'aRange'
		TABLE_PTR tail = ((Stream*)aRange.thePtr.theStream)->theTailTablePos;
		// check the pointer to be valid
		if (tail.set != ULLONG_MAX && tail.way != ULLONG_MAX) {
			// predict the successor pointer
			vector<TABLE_PTR > nexts = tail.MANA_table->table[tail.set][tail.way].next_ptr_ch.get_prediction();
			// set the pointer to the predicted one
			((Stream*)aRange.thePtr.theStream)->theTailTablePos = nexts[0];
		}
	};

	// record a new spatial region in MANA_TABLEs
	bool record(StreamEntry& e) {
		// do this block if MANA benefits from two MANA_TABLEs
		if (support_multiple_tables) {
			// fast lookup MANA_TABLE (multiple), if the entry hits in it, update the record in MANA_TABLE (multiple)
			// otherwise, record this entry in MANA_TABLE (single)
			bool flm = MANA_table_multiple->flookup(e.theRegionBase);
			if (flm) {
				return MANA_table_multiple->record(e);
			}
			else {
				return MANA_table_single->record(e);
			}
		}
		// just record the new entry in MANA_TABLE (single) if MANA uses a single TABLE
		else {
			return MANA_table_single->record(e);
		}
	};

	// get a pointer to a MANA_TABLE entry that records the given trigger address
	TABLE_PTR getPtr(uint64_t address) {
		// if MANA uses two tables, check both of them
		if (support_multiple_tables) {
			// call the getPtr method of both tables
			TABLE_PTR tptrs = MANA_table_single->getPtr(address);
			TABLE_PTR tptrm = MANA_table_multiple->getPtr(address);

			if (tptrs.MANA_table != NULL) {
				return tptrs;
			}
			else {
				return tptrm;
			}
		}
		else {
			return MANA_table_single->getPtr(address);
		}
	};

	// print MANA_TABLEs
	void printMANATable() {
		cout << "print single MANATable\n";
		MANA_table_single->printMANATable();
		if (support_multiple_tables) {
			cout << "print multiple MANATable\n";
			MANA_table_multiple->printMANATable();
		}
	}

	// print a MANA_TABLE entry that the given pointer points to it
	void printMANATableEntry(TABLE_PTR ptr) {
		if (ptr.MANA_table == NULL) {
			cout << "entry is NULL\n";
			return;
		}

		cout << "circular history size of table: " << ptr.MANA_table->circular_history_size << "\n";
		cout << "Set: " << ptr.set << "\n";
		cout << "Way: " << ptr.way << "\n";
		uint64_t address;
		if (ptr.MANA_table->PARTIAL_TAG_SHIFT_WIDTH) {
			address = ((((HOBPT.get(ptr.MANA_table->table[ptr.set][ptr.way].HOBP_index) << ptr.MANA_table->PARTIAL_TAG_SHIFT_WIDTH) + ptr.MANA_table->table[ptr.set][ptr.way].partial_tag) << ptr.MANA_table->num_of_sets_bits) + ptr.set) << LOG2_BLOCK_SIZE;
		}
		else {
			address = (((HOBPT.get(ptr.MANA_table->table[ptr.set][ptr.way].HOBP_index) >> (-1 * ptr.MANA_table->PARTIAL_TAG_SHIFT_WIDTH)) << ptr.MANA_table->num_of_sets_bits) + ptr.set) << LOG2_BLOCK_SIZE;
		}
		cout << "Address: " << address << "\n";
		cout << "partial_tag: " << ptr.MANA_table->table[ptr.set][ptr.way].partial_tag << "\n";
		cout << "HOBP_index: " << ptr.MANA_table->table[ptr.set][ptr.way].HOBP_index.first << "\t" << ptr.MANA_table->table[ptr.set][ptr.way].HOBP_index.second << "\n";
		cout << "footprint: " << ptr.MANA_table->table[ptr.set][ptr.way].footprint.to_ulong() << "\n";
		ptr.MANA_table->table[ptr.set][ptr.way].next_ptr_ch.print_history();
	}
};

// This struct represents MANA prefetcher's behavior
class MANA_PREFETCHER {
	// The followings are some counters to evaluate what happens in MANA prefetcher
	// They do not contribute to any functional behavior in MANA prefetcher
	uint64_t statHeadFound;
	uint64_t statHeadMissing;
	uint64_t statStreamBufferHit;
	uint64_t statPrefetchEntryFound;
	uint64_t statCompactorMatch;
	uint64_t statStreamTrackerLookup;
	uint64_t statRecord;
	uint64_t statGetPointer;
	uint64_t statEnqueuePrefetch;
	uint64_t statL1iLookups;
	uint64_t statStreamBufferLookups;
	uint64_t statCompactorLookups;
	uint64_t statPrefetchQueueIsFull;

	set<uint64_t> RegionBases;
	map<uint64_t, uint64_t> next_region;
	uint64_t next_region_correct, next_region_wrong, last_region;
	uint64_t traceLineBuffer;
	// The end of evaluation counters

	// Functional components of MANA are defined here
	StreamTracker* streamTracker; // This is acutally the SABs
	list<StreamEntry> SRQ;

	list<uint64_t> thePrefetchQueue;
	uint64_t thePrefetchQueueSize;

	MANA_TABLES *MANA_tables;

public:

	MANA_PREFETCHER()
	{
		statHeadFound = 0;
		statHeadMissing = 0;
		statStreamBufferHit = 0;
		statPrefetchEntryFound = 0;
		statCompactorMatch = 0;
		statStreamTrackerLookup = 0;
		statRecord = 0;
		statGetPointer = 0;
		statEnqueuePrefetch = 0;
		statL1iLookups = 0;
		statStreamBufferLookups = 0;
		statCompactorLookups = 0;
		statPrefetchQueueIsFull = 0;

		next_region_correct = 0;
		next_region_wrong = 0;
		last_region = 0;
		initialize();
	}

	// Initialization
	void initialize(void) {
		cout << "MANA initialize \n";
		if (ceil(log2(MANA_TABLE_SINGLE_SETS)) != floor(log2(MANA_TABLE_SINGLE_SETS))) {
			assert(false && "num_of_sets is not a power of two");
		}
		if (ceil(log2(MANA_TABLE_MULTIPLE_SETS)) != floor(log2(MANA_TABLE_MULTIPLE_SETS))) {
			assert(false && "num_of_sets is not a power of two");
		}

		MANA_tables = new MANA_TABLES(
		    MANA_TABLE_SINGLE_SETS,
		    MANA_TABLE_SINGLE_WAYS,
		    MANA_TABLE_SINGLE_CIRCULAR_HISTORY_SIZE,
		    MANA_TABLE_SINGLE_TAG_DOMAIN,
		    MANA_TABLE_MULTIPLE_SETS,
		    MANA_TABLE_MULTIPLE_WAYS,
		    MANA_TABLE_MULTIPLE_CIRCULAR_HISTORY_SIZE,
		    MANA_TABLE_MULTIPLE_TAG_DOMAIN,
		    MANA_SUPPORT_MULTIPLE_TABLES
		);

		cout << "tds: " << MANA_TABLE_SINGLE_TAG_DOMAIN << " tdm: " << MANA_TABLE_MULTIPLE_TAG_DOMAIN << "\n";
		streamTracker = new StreamTracker(theStreamCount, theTrackerSize, theLookahead);
		for (int i = 0; i < theSRQSize; i++) {
			SRQ.push_back(StreamEntry(i + 1, false));
		}
		traceLineBuffer = 0;
		thePrefetchQueueSize = 64;

		report_prefetcher_storage_cost();
	}

	void report_prefetcher_storage_cost() {
		// we have assumed a 64-bit address space
		uint64_t address_width = 64;

		// HOBP is an instruction address that:
		// 6 bits are removed because of block offset,
		// log2(MANA_TABLE_SINGLE_SETS) bits are removed because of set offset in MANA_TABLE,
		// MANA_TABLE_SINGLE_TAG_DOMAIN bits are removed because of the partial tag,
		// log2(HOBPT_sets) bits are removed because of set offset in HOBPT
		uint64_t HOBP = address_width - 6 - log2(MANA_TABLE_SINGLE_SETS) - MANA_TABLE_SINGLE_TAG_DOMAIN - log2(HOBPT_sets);

		// successor ptr should be able to address all MANA_TABLE entries
		uint64_t successor_ptr = ceil(log2(MANA_TABLE_SINGLE_SETS * MANA_TABLE_SINGLE_WAYS));
		if (MANA_SUPPORT_MULTIPLE_TABLES) {
			// max(): successor_ptr should be large enough to address both MANA_TABLEs
			// '+1' : is for a single bit that indicates the pointer belongs to which MANA_TABLE
			successor_ptr = max(ceil(log2(MANA_TABLE_SINGLE_SETS * MANA_TABLE_SINGLE_WAYS)), ceil(log2(MANA_TABLE_MULTIPLE_SETS * MANA_TABLE_MULTIPLE_WAYS))) + 1;
		}

		// HOBP_index should be large enough to address all HOBPT entries
		uint64_t HOBP_index = log2(HOBPT_sets * HOBPT_ways);

		// the size of MANA_TABLE_SINGLE is calculated as follows: (it is also the same for MULTIPLE)
		// (the number of all entries * the size of each entry)
		// the number of all entries: (MANA_TABLE_SINGLE_SETS * MANA_TABLE_SINGLE_WAYS)
		// each entry has:
		//  1) partial tag = MANA_TABLE_SINGLE_TAG_DOMAIN
		//  2) HOBP_index
		//  3) footprints: regionSize * MANA_TABLE_SINGLE_CIRCULAR_HISTORY_SIZE
		//  4) a pointer to successor: successor_ptr
		//  5) bits for replacement policy: ceil(MANA_TABLE_SINGLE_WAYS)
		double size_MANA_table = (MANA_TABLE_SINGLE_SETS * MANA_TABLE_SINGLE_WAYS) * (MANA_TABLE_SINGLE_TAG_DOMAIN + HOBP_index + regionSize + MANA_TABLE_SINGLE_CIRCULAR_HISTORY_SIZE * successor_ptr + ceil(log2(MANA_TABLE_SINGLE_WAYS))) / 8.0 / 1024.0;
//    cout << "entry size: " << MANA_TABLE_SINGLE_TAG_DOMAIN << " " << HOBP_index << " " << regionSize << " " << MANA_TABLE_SINGLE_CIRCULAR_HISTORY_SIZE << " " << successor_ptr << " " << ceil(MANA_TABLE_SINGLE_WAYS) << "\n";
		double size_MANA_table_2 = 0;

		if (MANA_SUPPORT_MULTIPLE_TABLES) {
			size_MANA_table_2 = (MANA_TABLE_MULTIPLE_SETS * MANA_TABLE_MULTIPLE_WAYS) * (MANA_TABLE_MULTIPLE_TAG_DOMAIN + HOBP_index + regionSize + ceil(log2(MANA_TABLE_MULTIPLE_CIRCULAR_HISTORY_SIZE)) + successor_ptr * MANA_TABLE_MULTIPLE_CIRCULAR_HISTORY_SIZE + ceil(log2(MANA_TABLE_MULTIPLE_WAYS))) / 8.0 / 1024.0;
		}

		// the size of HOBPT is calculated as follows:
		// the number of entries * the size of each entry
		// the number of entries = (HOBPT_sets * HOBPT_ways)
		// each entry:
		//  1) HOBP
		//  2) bits for replacement policy = ceil(log2(HOBPT_ways))
		double size_HOBPT = (HOBPT_sets * HOBPT_ways) * (HOBP + ceil(log2(HOBPT_ways))) / 8.0 / 1024.0;

		// the size of SRQ: the number of SRQ entries * the size of each SRQ entry
		// the number of SRQ entries: theSRQSize
		// the size of each SRQ entry: (a trigger address = address_width - 6) + (a footprint = regionSize)
		double size_SRQ = theSRQSize * (address_width - 6 + regionSize) / 8.0 / 1024.0;

		// the size of SABs = the number of SABs * the number of entries in each SAB * the size of each SAB entry
		double size_SABs = (theStreamCount * theTrackerSize * (address_width - 6 + regionSize)) / 8.0 / 1024.0;
		double size_prefetch_queue = thePrefetchQueueSize * (address_width - 6) / 8.0 / 1024.0;

		cout << "size MANA SINGLE: " << size_MANA_table << " KB\n";
		cout << "size MANA NULTIPLE: " << size_MANA_table_2 << " KB\n";
		cout << "size HOBPT: " << size_HOBPT << " KB\n";
		cout << "size SRQ: " << size_SRQ << " KB\n";
		cout << "size SABs: " << size_SABs << " KB\n";
		cout << "size prefetch queue: " << size_prefetch_queue << " KB\n";
		cout << "total: " << (size_HOBPT + size_MANA_table + size_MANA_table_2 + size_SRQ + size_SABs + size_prefetch_queue) << " KB\n";
	}

	void finalize(void) {
	}

private:

	// This function is the heart of MANA, everything is controled through here
	// The function's name is 'retire' since it was better to create MANA based on the retire-order instruction stream
	// However, since ChampSim does not enter the wrong-path simulation, MANA is created based on the fetch-stream
	void retire(uint64_t theAddress) {

		bool prefetched = false;

		// update an evaluation counter
		statStreamBufferLookups += (theStreamCount * theTrackerSize * 2);

		// Lookup the SABs
		Range aRange = Range();
		bool streamHit = streamTracker->lookup(theAddress, prefetched, aRange);
		statStreamTrackerLookup++;

		// Do the following if the new block misses in SABs
		if (!streamHit) {
			// Lookup MANA_TABLEs to know whether this block is the trigger address of an already observed spatial region
			TABLE_PTR table_ptr = MANA_tables->getPtr(theAddress);
			statGetPointer++;

			// allocate a new stream if a valid entry is found in MANA_TABLEs
			if (table_ptr.set != ULLONG_MAX && table_ptr.way != ULLONG_MAX) {
				statHeadFound++;
				aRange = streamTracker->allocate(table_ptr);
			}
			else {
				statHeadMissing++;
			}
		}
		else {
			if (prefetched) {
				statStreamBufferHit++;
			}
		}

		// aRange.theLegth determines the number of spatial regions that should be prefetched to provide the sufficient lookahead ahead of the fetch stream
		if (aRange.theLength) {
			// start pointer chasing!
			for (int i = 0; i < aRange.theLength; i++) {

				// Read the spatial region using the pointer that is kept in the SAB
				StreamEntry aPrefetchEntry;
				bool readHit = MANA_tables->read(aRange, aPrefetchEntry);

				if (!readHit) {
					break;
				}

				// extract the prefetch candidates of the new fetched spatial region
				vector<uint64_t> prefetch_candidates = aPrefetchEntry.getPrefetchCandidates();
				bool issuedAll = true;
				for (uint32_t j = 0; j < prefetch_candidates.size(); j++) {
					// push the prefetch candidates into 'thePrefetchQueue'
					if (thePrefetchQueue.size() >= thePrefetchQueueSize) {
						issuedAll = false;
						break;
					}
					thePrefetchQueue.push_back(prefetch_candidates[j]);

					// update some evaluation counters
					statEnqueuePrefetch++;
					statL1iLookups += 8;
				}

				if (!issuedAll) {
					statPrefetchQueueIsFull++;
					break;
				}

				// a new spatial region is fetched from MANA_TABLEs, push it to the SAB
				statPrefetchEntryFound++;
				streamTracker->push_back(aRange.thePtr, aPrefetchEntry);

				// chase the pointer, go to the successor spatial region
				MANA_tables->incrementPointer(aRange);
			}
		}

		// update an evaluation counter
		statCompactorLookups += (theSRQSize * 2);

		// update the SRQ
		bool evict = true;
		// iterate over the SRQ entries
		for (list<StreamEntry>::iterator i = SRQ.begin(); i != SRQ.end(); i++) {
			bool prefetched;
			// check whether the new observed block falls in the address space covered by a tracked spatial region in the SRQ
			if (i->inRange(theAddress, prefetched)) {
				pair<int, bool> index = i->getIndex(theAddress);
				// set the bit corresponds to the arriving block in the spatial region's footprint
				if (index.second) {
					i->bits.set(index.first);
				}

				// update an evaluation counter
				statCompactorMatch++;

				// should not evict an entry, it is matched
				evict = false;
				break;
			}
		}

		// no match found in SRQ, an entry should be evicted, the victim should be recorded in the MANA_TABLEs, and a new spatial regions should be tracked in the SRQ
		if (evict) {
			// the victim is in front of the SRQ
			StreamEntry victim = SRQ.front();
			SRQ.pop_front();

			// the new spatial region is pushed into the SRQ
			SRQ.push_back(StreamEntry(theAddress)); //add the new address to the SRQ

			// record the victim in the MANA_TABLEs
			MANA_tables->record(victim);

			// The following code block updates some evaluation counters
			{
				RegionBases.insert(victim.theRegionBase >> LOG2_BLOCK_SIZE);
				if (next_region.find(last_region) != next_region.end()) {
					if (next_region[last_region] == victim.theRegionBase) {
						next_region_correct++;
					}
					else {
						next_region_wrong++;
					}
				}
				next_region[last_region] = victim.theRegionBase;
				last_region = victim.theRegionBase;
				statRecord++;
			}
		}
	}

public:
	// The interface of MANA prefetcher, it has to functions, the first pushes the fetch stream, the second one pulls the prefetch candidates

	// This function informs MANA prefetch that a new block is observed in the fetch stream
	// To avoid disturbing MANA with repetitive requests on a single block, this interface
	// updates MANA when the observed block is different from the last observed block
	void pushRetireIn(uint64_t theAddress) {
		if (traceLineBuffer == (theAddress & theBlockMask)) {
			return;
		}
		traceLineBuffer = theAddress & theBlockMask;

		// inform MANA that a new block is observed
		retire(theAddress & theBlockMask);
	}

	// This function offers a prefetch candidate that is at the head of the prefech queue
	uint64_t pullPrefetchOut() {
		if (!thePrefetchQueue.empty()) {
			uint64_t thePrefetchAddress = thePrefetchQueue.front();
			//thePrefetchQueue.pop_front();
			return thePrefetchAddress;
		}
		else {
			return 0;
		}
	}

	void popPrefetch() {
		thePrefetchQueue.pop_front();
	}

	// This function prints the content of MANA_TABLEs, it may produce a very large content, use it CAREFULLY!
	void printMANATables() {
		MANA_tables->printMANATable();
	}

	// This function reports the statCounters of MANA prefetcher
	void getStats() {
		cout << "statHeadFound: " << statHeadFound << "\n";
		cout << "statHeadMissing: " << statHeadMissing << "\n";
		cout << "statStreamBufferHit: " << statStreamBufferHit << "\n";
		cout << "statPrefetchEntryFound: " << statPrefetchEntryFound << "\n";
		cout << "statCompactorMatch: " << statCompactorMatch << "\n";
		cout << "statStreamTrackerLookup: " << statStreamTrackerLookup << "\n";
		cout << "statRecord: " << statRecord << "\n";
		cout << "statGetPointer: " << statGetPointer << "\n";
		cout << "statEnqueuePrefetch: " << statEnqueuePrefetch << "\n";
		cout << "statPrefetchQueueIsFull: " << statPrefetchQueueIsFull << "\n";

		cout << "StreamBufferHitRate: " << (double)statStreamBufferHit / statStreamTrackerLookup << "\n";
		cout << "Regions' size: " << RegionBases.size() << "\n";
		cout << "next_region_correct: " << next_region_correct << "\n";
		cout << "next_region_wrong: " << next_region_wrong << "\n";
		cout << "next_region_correct_prediction: " << (double)next_region_correct / (next_region_correct + next_region_wrong) << "\n";
		cout << "statStreamBufferLookups: " << statStreamBufferLookups << "\n";
		cout << "statL1iLookups: " << statL1iLookups << "\n";
		cout << "statCompactorLookups: " << statCompactorLookups << "\n";
	}
};

// instantiate the MANA prefetcher
MANA_PREFETCHER MANA = MANA_PREFETCHER();

} //End Namespace MANA

// MANA does not use this interface
void CACHE::prefetcher_initialize()
{
}

// MANA does not use this interface
void CACHE::prefetcher_branch_operate(uint64_t ip, uint8_t branch_type, uint64_t branch_target)
{
}

// MANA tracks the fetch stream, the observed blocks are sent to MANA prefetcher to make the required processes
uint32_t CACHE::prefetcher_cache_operate(uint64_t addr, uint64_t ip, uint8_t cache_hit, bool prefetch_hit, uint8_t type, uint32_t metadata_in)
{
	nMANA::MANA.pushRetireIn(addr);
        return metadata_in;
}

// MANA checks whether it has a prefetch candidate, it issues its prefetch candidates
// We have assumed that MANA issues a single prefetch request each cycle to be closer to a practical design
void CACHE::prefetcher_cycle_operate()
{
	while (true) {
		uint64_t prefetched_line = nMANA::MANA.pullPrefetchOut();
		bool prefetchIssued = false;
		if (prefetched_line != 0) {
                  prefetchIssued = prefetch_line(prefetched_line, true, 0);
			if (prefetchIssued) {
				nMANA::MANA.popPrefetch();
			}
			else {
				break;
			}
		}
		else {
			break;
		}
	}
}

// MANA does not use this interface
uint32_t CACHE::prefetcher_cache_fill(uint64_t v_addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_v_addr, uint32_t metadata_in) {
  return metadata_in;
}


// print the state of the prefetcher and some counters
void CACHE::prefetcher_final_stats()
{
	cout << "\n\n\nPrinting Logs\n";
	// nMANA::MANA.printMANATables();
	nMANA::MANA.getStats();
}