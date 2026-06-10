# lob

A single-instrument limit order book and matching engine in C++20 with price-time priority. Price levels live in a flat array with a bitmap for finding the best price, orders come from a preallocated pool so matching never touches the heap, and a lock-free single-producer single-consumer ring buffer feeds the matcher thread. A differential fuzz test checks every event against a naive `std::map` book.
